#include "stream.h"

#include "model/app_config.h"
#include "model/stream/audio/audio_config.h"
#include "model/stream/audio/file/raw_file_audio_sink.h"
#include "model/stream/audio/odas/odas_audio_source.h"
#include "model/stream/audio/odas/odas_client.h"
#include "model/stream/audio/odas/odas_position_source.h"
#include "model/stream/audio/pulseaudio/pulseaudio_sink.h"
#include "model/stream/stream_config.h"
#include "model/stream/utils/images/images.h"
#include "model/stream/utils/models/dim2.h"
#include "model/stream/utils/models/spherical_angle_rect.h"
#include "model/stream/utils/threads/lock_triple_buffer.h"
#include "model/stream/utils/threads/readerwriterqueue.h"
#include "model/stream/video/detection/darknet_config.h"
#include "model/stream/video/dewarping/models/dewarping_config.h"
#include "model/stream/video/impl/implementation_factory.h"
#include "model/stream/video/input/dewarped_video_input.h"
#include "model/stream/video/output/virtual_camera_output.h"
#include "model/stream/video/video_config.h"

#include <string>
#include <vector>

#include <QCoreApplication>

namespace
{
const int IMAGE_BUFFER_COUNT = 10;
const int AUDIO_BUFFER_COUNT = 2;

// TODO: config
const int ODAS_AUDIO_PORT = 10030;
const int ODAS_POSITION_PORT = 10020;

// TODO: config
float CLASSIFIER_RANGE_THRESHOLD = 0.26f; // ~15 degrees
}

namespace Model
{
Stream::Stream(std::shared_ptr<Config> config)
    : m_state(IStream::State::Stopped)
    , m_mediaThread(nullptr)
    , m_config(config)
    , m_implementationFactory(false)
{
    std::shared_ptr<AudioConfig> audioInputConfig = m_config->audioInputConfig();
    std::shared_ptr<AudioConfig> audioOutputConfig = m_config->audioOutputConfig();
    std::shared_ptr<VideoConfig> videoOutputConfig = m_config->videoOutputConfig();
    std::shared_ptr<VideoConfig> videoInputConfig = m_config->videoInputConfig();
    std::shared_ptr<StreamConfig> streamConfig = m_config->streamConfig();
    std::shared_ptr<DarknetConfig> darknetConfig = m_config->darknetConfig();
    std::shared_ptr<DewarpingConfig> dewarpingConfig = m_config->dewarpingConfig();

    float aspectRatio = streamConfig->value(StreamConfig::ASPECT_RATIO_WIDTH).toFloat() /
                        streamConfig->value(StreamConfig::ASPECT_RATIO_HEIGHT).toFloat();
    float minElevation = streamConfig->value(StreamConfig::MIN_ELEVATION).toFloat();
    float maxElevation = streamConfig->value(StreamConfig::MAX_ELEVATION).toFloat();
    Dim2<int> resolution(videoInputConfig->value(VideoConfig::WIDTH).toInt(),
                         videoInputConfig->value(VideoConfig::HEIGHT).toInt());

    int fps = videoOutputConfig->value(VideoConfig::FPS).toInt();
    if (fps == 0)
    {
        throw std::invalid_argument("Error in Stream - Fps cannot be 0");
    }
    int audioChunkDurationMs = 1000 / fps;

    int sleepBetweenLayersForwardUs = darknetConfig->value(DarknetConfig::SLEEP_BETWEEN_LAYERS_FORWARD_US).toInt();
    std::string configFile =
        (QCoreApplication::applicationDirPath() + "/../configs/yolo/cfg/yolov3-tiny.cfg").toStdString();
    std::string weightsFile =
        (QCoreApplication::applicationDirPath() + "/../configs/yolo/weights/yolov3-tiny.weights").toStdString();
    std::string metaFile = (QCoreApplication::applicationDirPath() + "/../configs/yolo/cfg/coco.data").toStdString();

    m_imageBuffer = std::make_shared<LockTripleBuffer<RGBImage>>(RGBImage(resolution));

    m_objectFactory = m_implementationFactory.getDetectionObjectFactory();
    m_objectFactory->allocateObjectLockTripleBuffer(*m_imageBuffer);

    std::unique_ptr<DetectionThread> detectionThread = std::make_unique<DetectionThread>(
        m_imageBuffer,
        m_implementationFactory.getDetector(configFile, weightsFile, metaFile, sleepBetweenLayersForwardUs),
        m_implementationFactory.getDetectionFisheyeDewarper(aspectRatio),
        m_implementationFactory.getDetectionObjectFactory(), m_implementationFactory.getDetectionSynchronizer(),
        dewarpingConfig);

    std::shared_ptr<IPositionSource> odasPositionSource = std::make_shared<OdasPositionSource>(ODAS_POSITION_PORT);

    std::shared_ptr<VirtualCameraManager> virtualCameraManager = std::make_shared<VirtualCameraManager>(aspectRatio, minElevation, maxElevation);

    std::unique_ptr<IVideoInput> dewarpedVideoInput = std::make_unique<DewarpedVideoInput>(
        m_implementationFactory.getCameraReader(videoInputConfig), m_implementationFactory.getFisheyeDewarper(),
        m_implementationFactory.getObjectFactory(), m_implementationFactory.getSynchronizer(),
        virtualCameraManager, std::move(detectionThread), m_imageBuffer,
        m_implementationFactory.getImageConverter(), odasPositionSource, dewarpingConfig, videoInputConfig, videoOutputConfig,
        IMAGE_BUFFER_COUNT, CLASSIFIER_RANGE_THRESHOLD);

    m_mediaThread = std::make_unique<MediaThread>(
        std::make_unique<OdasAudioSource>(ODAS_AUDIO_PORT, audioChunkDurationMs, AUDIO_BUFFER_COUNT, audioInputConfig),
        std::make_unique<PulseAudioSink>(audioOutputConfig),
        odasPositionSource,
        std::move(dewarpedVideoInput),
        std::make_unique<VirtualCameraOutput>(videoOutputConfig),
        virtualCameraManager,
        std::make_unique<MediaSynchronizer>(audioChunkDurationMs * 1000),
        fps, CLASSIFIER_RANGE_THRESHOLD);

    m_odasClient = std::make_unique<OdasClient>(m_config->appConfig());
    m_odasClient->attach(this);
}

Stream::~Stream()
{
    m_objectFactory->deallocateObjectLockTripleBuffer(*m_imageBuffer);
}

/**
 * @brief Start children threads.
 */
void Stream::start()
{
    if (m_state == IStream::State::Stopped)
    {
        m_mediaThread->start();
        m_odasClient->start();
        updateState(IStream::State::Started);
    }
}

/**
 * @brief Stop children threads.
 */
void Stream::stop()
{
    updateState(IStream::State::Stopping);

    if (m_odasClient->getState() != Thread::ThreadStatus::CRASHED)
    {
        m_odasClient->stop();
        m_odasClient->join();
    }

    if (m_mediaThread->getState() != Thread::ThreadStatus::CRASHED)
    {
        m_mediaThread->stop();
        m_mediaThread->join();
    }

    updateState(IStream::State::Stopped);
}

/**
 * @brief Wait that all children threads terminate, do not call if you called Stream::stop
 */
void Stream::join()
{
    m_odasClient->join();
    m_mediaThread->join();
}

void Stream::updateState(const IStream::State& state)
{
    m_state = state;
    emit stateChanged(m_state);
}

/**
 * @brief What to do when a child thread crash.
 */
void Stream::updateObserver()
{
    const Thread::ThreadStatus odasClientState = m_odasClient->getState();
    const Thread::ThreadStatus mediaState = m_mediaThread->getState();
    if (odasClientState == Thread::ThreadStatus::CRASHED || mediaState == Thread::ThreadStatus::CRASHED)
    {
        stop();
    }
}
}    // namespace Model
