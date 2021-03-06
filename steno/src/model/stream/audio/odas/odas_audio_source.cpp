#include "odas_audio_source.h"

#include <cmath>

#include <QDebug>
#include <QTcpServer>
#include <QTcpSocket>

namespace Model
{
OdasAudioSource::OdasAudioSource(int port, int desiredChunkDurationMs, int numberOfBuffers,
                                 std::shared_ptr<AudioConfig> audioConfig)
    : port_(port)
    , audioConfig_(audioConfig)
    // We add 10 buffers to make sure we don't overwrite data when reading from the socket
    , audioChunks_(numberOfBuffers + 10, AudioChunk(desiredChunkDurationMs / 1000.f * audioConfig_->rate,
                                               audioConfig_->channels, audioConfig_->formatBytes))
    , audioQueue_(std::make_shared<moodycamel::BlockingReaderWriterQueue<AudioChunk>>(numberOfBuffers))
{
    for (int i = 0; i < audioChunks_.size(); i++)
    {
        audioChunks_.current().audioData =
            std::shared_ptr<uint8_t>(new uint8_t[audioChunks_.current().size], std::default_delete<uint8_t[]>());
        audioChunks_.next();
    }
}

OdasAudioSource::~OdasAudioSource()
{
    close();
}

void OdasAudioSource::open()
{
    start();
}

void OdasAudioSource::close()
{
    requestInterruption();
}

void OdasAudioSource::run()
{
    QTcpSocket* socket = nullptr;

    std::unique_ptr<QTcpServer> server = std::make_unique<QTcpServer>();
    server->setMaxPendingConnections(1);
    server->listen(QHostAddress::Any, port_);

    qDebug() << "Odas audio source thread started";

    unsigned int readIndex = 0;
    while (!isInterruptionRequested())
    {
        if (socket == nullptr)
        {
            if (server->waitForNewConnection(1))
            {
                socket = server->nextPendingConnection();
            }
        }
        else
        {
            if (socket->state() == QAbstractSocket::ConnectedState)
            {
                if (socket->bytesAvailable() >= audioConfig_->packetAudioSize)
                {
                    AudioChunk& audioChunk = audioChunks_.current();

                    unsigned long long currentTimeStamp = 0;
                    socket->read(reinterpret_cast<char*>(&currentTimeStamp), audioConfig_->packetHeaderSize);
                    
                    if (readIndex == 0)
                    {
                        audioChunk.timestamp = currentTimeStamp;
                    }

                    int remainingChunkSpace = audioChunk.size - readIndex;
                    int bytesToRead = remainingChunkSpace < audioConfig_->packetAudioSize
                                          ? remainingChunkSpace
                                          : audioConfig_->packetAudioSize;

                    int firstBytesRead =
                        socket->read(reinterpret_cast<char*>(audioChunk.audioData.get()) + readIndex, bytesToRead);
                    readIndex += firstBytesRead;

                    // There is still data to be read if the chunk didn't have enough space
                    int remainingBytesToRead = audioConfig_->packetAudioSize - firstBytesRead;

                    // Current chunk is full
                    if (readIndex == audioChunk.size)
                    {
                        audioChunks_.next();
                        readIndex = 0;

                        // Start a new chunk if there is still data in the packet
                        if (remainingBytesToRead > 0)
                        {
                            AudioChunk& newAudioChunk = audioChunks_.current();

                            unsigned long long newTimeStamp = calculateNewTimestamp(currentTimeStamp, bytesToRead);
                            newAudioChunk.timestamp = newTimeStamp;

                            // Read remaining packet data in the new chunk
                            int secondBytesRead =
                                socket->read(reinterpret_cast<char*>(newAudioChunk.audioData.get()) + readIndex,
                                             remainingBytesToRead);
                            readIndex += secondBytesRead;
                        }

                        // Output audio chunk, if queue is full keep trying...
                        bool success = false;
                        while (!success && !isInterruptionRequested())
                        {
                            success = audioQueue_->try_enqueue(audioChunk);

                            if (!success)
                            {
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            }
                        }
                    }
                }
                else
                {
                    socket->waitForReadyRead(1);
                }
            }
            else
            {
                // reset read
                readIndex = 0;
                delete socket;
                socket = nullptr;
            }
        }
    }

    qDebug() << "Odas audio source thread stopped";
}

bool OdasAudioSource::readAudioChunk(AudioChunk& outAudioChunk)
{
    return audioQueue_->try_dequeue(outAudioChunk);
}

unsigned long long OdasAudioSource::calculateNewTimestamp(unsigned long long currentTimestamp, int bytesForward)
{
    int numberOfSamples = bytesForward / audioConfig_->formatBytes / audioConfig_->channels;
    unsigned long long microseconds = numberOfSamples * std::pow(10, 6) / audioConfig_->rate;
    return currentTimestamp + microseconds;
}

}    // namespace Model
