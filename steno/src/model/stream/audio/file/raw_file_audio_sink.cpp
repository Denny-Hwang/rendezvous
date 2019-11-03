#include "raw_file_audio_sink.h"

#include <iostream>

namespace Model
{
RawFileAudioSink::RawFileAudioSink(std::string fileName)
    : m_file(nullptr)
    , m_fileName(std::move(fileName))
{
}

RawFileAudioSink::~RawFileAudioSink()
{
    close();
}

void RawFileAudioSink::open()
{
    m_file = fopen(m_fileName.c_str(), "ab");
    if (m_file == nullptr)
    {
        throw std::runtime_error("cannot open file");
    }
}

void RawFileAudioSink::close()
{
    fclose(m_file);
}

int RawFileAudioSink::write(uint8_t* buffer, int bytesToWrite)
{
    return fwrite(buffer, sizeof(buffer[0]), bytesToWrite, m_file);
}
}    // namespace Model