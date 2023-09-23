#include <stdio.h>
#include <cstdint>
#include <algorithm>
#include <fstream>
#include <vector>
#include <chrono>
#include <iostream>
#include <string>

static const uint32_t RIFF_ID = 0x46464952;//RIFF
static const uint32_t WAVE_ID = 0x45564157;//WAVE
static const uint32_t FORMAT_ID = 0x20746D66;//fmt 
static const uint32_t DATA_ID = 0x61746164;//data

static const uint16_t steps[89] = 
{
	7, 8, 9, 10, 11, 12, 13, 14,
	16, 17, 19, 21, 23, 25, 28, 31,
	34, 37, 41, 45, 50, 55, 60, 66,
	73, 80, 88, 97, 107, 118, 130, 143,
	157, 173, 190, 209, 230, 253, 279, 307,
	337, 371, 408, 449, 494, 544, 598, 658,
	724, 796, 876, 963, 1060, 1166, 1282, 1411,
	1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
	3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
	7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767
};
static const int8_t stepIndices[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

struct IMAState
{
	int16_t predictedSample;
	uint8_t stepIndex;
};

struct FormatChunk
{
	uint32_t id;
	uint32_t size;
	uint16_t formatTag;
	uint16_t channels;
	uint32_t sampleRate;
	uint32_t byteRate;
	uint16_t blockAlign;
	uint16_t bitsPerSample;
};

struct DataChunk
{
	uint32_t id;
	uint32_t size;
};

struct RIFFChunk
{
	uint32_t id;
	uint32_t size;
	uint32_t formatID;
	FormatChunk format;
	DataChunk data;
};

int16_t decodeSample(uint8_t sample, IMAState& state)
{
	uint16_t step = steps[state.stepIndex];
	int16_t diff = step >> 3;

	if (sample & 1) diff += step >> 2;
	if (sample & 2) diff += step >> 1;
	if (sample & 4) diff += step;
	if (sample & 8) diff = -diff;

	state.predictedSample = std::clamp(state.predictedSample + diff, -32768, 32767);
	state.stepIndex = std::clamp(state.stepIndex + stepIndices[sample & 7], 0, 88);
	return state.predictedSample;
}

uint8_t encodeSample(int16_t sample, IMAState& state)
{
	uint16_t step = steps[state.stepIndex];
	int16_t diff = step >> 3;
	int16_t sampleDiff = sample - state.predictedSample;
	uint8_t encodedSample = 0;

	if (sampleDiff < 0)
	{
		encodedSample = 8;
		sampleDiff = -sampleDiff;
	}

	if (sampleDiff >= step)
	{
		encodedSample |= 4;
		sampleDiff -= step;
		diff += step;
	}

	step >>= 1;

	if (sampleDiff >= step)
	{
		encodedSample |= 2;
		sampleDiff -= step;
		diff += step;
	}

	step >>= 1;

	if (sampleDiff >= step)
	{
		encodedSample |= 1;
		diff += step;
	}

	if (encodedSample & 8)
		diff = -diff;

	state.predictedSample = std::clamp(state.predictedSample + diff, -32768, 32767);
	state.stepIndex = std::clamp(state.stepIndex + stepIndices[encodedSample & 7], 0, 88);
	return encodedSample;
}

std::vector<int16_t> decode(std::vector<uint8_t> samples, uint16_t channels = 1, uint16_t bitsPerSample = 16)
{
	std::vector<int16_t> outputData;
	size_t size = samples.size();

	if (channels == 1)
	{
		IMAState state = { 0, 0 };

		outputData.resize(size * 2);

		for (uint32_t i = 0; i < size; i++)
		{
			uint8_t sample = samples[i];

			outputData[i * 2 + 0] = decodeSample(sample & 0xF, state);
			outputData[i * 2 + 1] = decodeSample((sample >> 4) & 0xF, state);
		}
	}
	else if (channels == 2)
	{
		IMAState leftState = { 0, 0 }, rightState = { 0, 0 };

		outputData.resize(size * 2);

		for (uint32_t i = 0; i < size; i++)
		{
			uint8_t sample = samples[i];

			outputData[i * 2 + 0] = decodeSample(sample & 0xF, leftState);
			outputData[i * 2 + 1] = decodeSample((sample >> 4) & 0xF, rightState);
		}
	}
	else if (channels == 4)
	{
		uint32_t blockCount = size / 16384U;
		uint32_t lastBlockSize = size - (blockCount * 16384U);
		uint32_t realSize = size - (16384U - lastBlockSize);
		IMAState leftState = { 0, 0 }, rightState = { 0, 0 }, centerState = { 0, 0 }, lfeState = { 0, 0 };

		outputData.resize(realSize * 2);

		for (uint32_t i = 0; i < size; i += 16384U)
		{
			size_t blockSize = std::min(16384U, size - i);

			for (uint16_t j = 0; j < blockSize - 8192U; j++)
			{
				uint8_t sample1 = samples[i + j];
				uint8_t sample2 = samples[i + j + 8192U];
				uint32_t index = (i * 2) + (j * 4);

				outputData[index + 0] = decodeSample(sample1 & 0xF, leftState);
				outputData[index + 1] = decodeSample((sample1 >> 4) & 0xF, rightState);
				outputData[index + 2] = decodeSample(sample2 & 0xF, centerState);
				outputData[index + 3] = decodeSample((sample2 >> 4) & 0xF, lfeState);
			}
		}
	}

	return outputData;
}

std::vector<uint8_t> encode(std::vector<int16_t> samples, uint16_t channels = 1U, uint16_t bitsPerSample = 16U)
{
	std::vector<uint8_t> outputData;
	uint32_t sampleCount = samples.size();

	if (channels == 1)
	{
		IMAState state = { 0, 0 };

		outputData.resize(sampleCount * 2 / 4);

		for (uint32_t i = 0; i < sampleCount / 2; i++)
		{
			uint8_t sample0 = encodeSample(samples[i * 2 + 0], state);
			uint8_t sample1 = encodeSample(samples[i * 2 + 1], state);

			outputData[i] = (sample1 << 4) | sample0;
		}
	}
	else if (channels == 2)
	{
		IMAState leftState = { 0, 0 }, rightState = { 0, 0 };

		outputData.resize(sampleCount * 2 / 4);

		for (uint32_t i = 0; i < sampleCount / 2; i++)
		{
			uint8_t leftSample = encodeSample(samples[i * 2 + 0], leftState);
			uint8_t rightSample = encodeSample(samples[i * 2 + 1], rightState);

			outputData[i] = (rightSample << 4) | leftSample;
		}
	}
	else if (channels == 4)
	{
		uint32_t size = sampleCount * 2 / 4;
		uint32_t blockCount = (size + 16383U) / 16384U;
		uint32_t lastBlockPadding = (blockCount * 16384U) - size;
		uint32_t realSize = size + (lastBlockPadding / 2);
		IMAState leftState = { 0, 0 }, rightState = { 0, 0 }, centerState = { 0, 0 }, lfeState = { 0, 0 };

		outputData.resize(realSize);

		for (uint32_t i = 0; i < realSize; i += 16384U)
		{
			size_t blockSize = std::min(16384U, realSize - i);

			for (uint16_t j = 0; j < blockSize - 8192U; j++)
			{
				uint32_t index = (i * 2) + (j * 4);
				uint8_t leftSample = encodeSample(samples[index + 0], leftState);
				uint8_t rightSample = encodeSample(samples[index + 1], rightState);
				uint8_t centerSample = encodeSample(samples[index + 2], centerState);
				uint8_t lfeSample = encodeSample(samples[index + 3], lfeState);

				outputData[i + j] = (rightSample << 4) | leftSample;
				outputData[i + j + 8192U] = (lfeSample << 4) | centerSample;
			}
		}
	}

	return outputData;
}

int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		std::cout << "Usage: " << argv[0] << " <inputFile> <outputFile>" << std::endl;
		return 1;
	}

	auto start_time = std::chrono::high_resolution_clock::now();
	std::ios::sync_with_stdio(false);

	const char* inputFileName = argv[1];
	const char* outputFileName = argv[2];
	std::ifstream inputFile(inputFileName, std::ios::binary);

	if (!inputFile)
		return -1;

	std::vector<int16_t> samples;
	uint16_t channels = 0, bitsPerSample = 0;

	while (!inputFile.eof())
	{
		uint32_t chunkID = 0, chunkSize = 0;

		inputFile.read(reinterpret_cast<char*>(&chunkID), 4);

		if (inputFile.tellg() <= 4 && chunkID != RIFF_ID)
		{
			return -2;
		}

		inputFile.read(reinterpret_cast<char*>(&chunkSize), 4);

		if (chunkSize == 0)
			continue;

		if (chunkID == RIFF_ID)
		{
			uint32_t formatID = 0;

			inputFile.read(reinterpret_cast<char*>(&formatID), 4);

			if (formatID != WAVE_ID)
			{
				return -3;
			}
		}
		else if (chunkID == FORMAT_ID)
		{
			if (chunkSize < 16)
			{
				return -4;
			}

			uint16_t formatTag = 0;

			inputFile.read(reinterpret_cast<char*>(&formatTag), 2);

			if (formatTag != 1)
			{
				return -5;
			}

			inputFile.read(reinterpret_cast<char*>(&channels), 2);

			if (channels != 1 && channels != 2 && channels != 4)
			{
				return -6;
			}

			inputFile.seekg(10, std::ios::cur);
			inputFile.read(reinterpret_cast<char*>(&bitsPerSample), 2);

			if (/*bitsPerSample != 8 &&*/ bitsPerSample != 16)
			{
				return -7;
			}

			if (chunkSize > 16)
				inputFile.seekg(chunkSize - 16, std::ios::cur);
		}
		else if (chunkID == DATA_ID)
		{
			samples.resize(chunkSize / 2);
			inputFile.read(reinterpret_cast<char*>(samples.data()), chunkSize);
			break;
		}
		else
		{
			inputFile.seekg(chunkSize, std::ios::cur);
		}
	}

	inputFile.close();

	if (samples.empty())
	{
		return -8;
	}

	std::vector<uint8_t> outputData = encode(samples, channels, bitsPerSample);
	std::ofstream outputFile(outputFileName, std::ios::binary);

	if (!outputFile)
		return -1;

	outputFile.write(reinterpret_cast<const char*>(outputData.data()), outputData.size());
	outputFile.close();

	auto elapsed_time = std::chrono::high_resolution_clock::now() - start_time;
	auto elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_time).count();
	std::cout << "Program has been running for " << elapsed_time_ms << " ms" << std::endl;
	return 0;
}

int main2(int argc, char* argv[])
{
	uint16_t channels = 2;
	uint32_t sampleRate = 22050;
	uint16_t bitsPerSample = 16;

	auto start_time = std::chrono::high_resolution_clock::now();
	std::ios::sync_with_stdio(false);

	const char* inputFileName = "C:/heli1_a.wav";
	const char* outputFileName = "C:/heli1_a_decoded.wav";
	std::ifstream inputFile(inputFileName, std::ios::binary);

	if (!inputFile)
		return -1;

	inputFile.seekg(0L, std::ios::end);

	std::streampos size = inputFile.tellg();
	std::vector<uint8_t> inputData(size);

	inputFile.seekg(0L, std::ios::beg);
	inputFile.read(reinterpret_cast<char*>(inputData.data()), size);
	inputFile.close();

	std::vector<int16_t> outputData = decode(inputData, channels, bitsPerSample);
	std::ofstream outputFile(outputFileName, std::ios::binary);

	if (!outputFile)
		return -1;

	RIFFChunk riff{};

	riff.id = RIFF_ID;
	riff.formatID = WAVE_ID;
	riff.format = {};
	riff.data = {};

	riff.format.id = FORMAT_ID;
	riff.format.size = sizeof(FormatChunk) - 8;
	riff.format.formatTag = 1;
	riff.format.channels = channels;
	riff.format.sampleRate = sampleRate;
	riff.format.byteRate = sampleRate * channels * (bitsPerSample / 8);
	riff.format.blockAlign = channels * (bitsPerSample / 8);
	riff.format.bitsPerSample = bitsPerSample;

	riff.data.id = DATA_ID;
	riff.data.size = outputData.size() * 2;
	riff.size = (sizeof(RIFFChunk) - 8) + riff.data.size;

	outputFile.write(reinterpret_cast<const char*>(&riff), sizeof(RIFFChunk));
	outputFile.write(reinterpret_cast<const char*>(outputData.data()), riff.data.size);
	outputFile.close();

	auto current_time = std::chrono::high_resolution_clock::now();
	std::cout << "Program has been running for " << std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time).count() << std::endl;
	return 0;
}