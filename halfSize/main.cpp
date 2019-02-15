#include <fstream>
#include <stdio.h>

#pragma region TGA
#pragma pack(push, 1)
struct TGA_Header
{
	char idlength;
	char colormaptype;
	char datatypecode;
	short colormaporigin;
	short colormaplength;
	char colormapdepth;
	short x_origin;
	short y_origin;
	short width;
	short height;
	char bitsperpixel;
	char imagedescriptor;
};
#pragma pack(pop)

static_assert(sizeof(TGA_Header) == 18, "TGA Header must be 18 byte aligned");

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define clamp(test, minVal, maxVal) (min(max(test, minVal), maxVal))

using BYTE = unsigned char;

static bool GetTGAHeader(std::ifstream& _file, TGA_Header* _header);

#pragma endregion

#pragma region IO

static bool GetImageDataRaw(std::ifstream& _file, BYTE* _data, size_t _sizeInBytes);
static bool
GetImageDataRLE(std::ifstream& _file, BYTE* _data, size_t _sizeInBytes, char _BytesPerPixel);

static bool WriteRAWToFile(std::ofstream& _file, BYTE* _data, size_t _sizeInBytes);
static bool WriteRLEToFile(std::ofstream& _file, BYTE* _data, size_t _sizeInBytes, char _bpp);

#pragma endregion

#pragma region Resizing
static void ResizeImage(BYTE* outData,
						short _outWidth,
						short _outHeight,
						BYTE* inData,
						short _inWidth,
						short _inHeight,
						char bpp);

static BYTE BilinearFilteredAtChannel(
	BYTE* tex, int texSizeX, int texSizeY, int bpp, int channel, double u, double v);
#pragma endregion

int main(int argc, char** argv)
{
	if(argc < 3)
	{
		printf_s("Usage: input.tga output.tga\n");
		return 0;
	}

	printf_s("Reading \"%s\"...\n", argv[1]);
	std::ifstream in(argv[1], std::ios::binary);
	if(false == in.is_open())
	{
		fprintf_s(stderr, "Failed to open \"%s\"\n", argv[1]);
		return 1;
	}

	TGA_Header inHeader;

	if(false == GetTGAHeader(in, &inHeader))
	{
		fprintf_s(stderr, "Failed to read TGA header from \"%s\"\n", argv[1]);
		return 1;
	}

	char bytesPerPixel = inHeader.bitsperpixel >> 3; // Convert to BytesPerPixel instead

	short inWidth	 = inHeader.width;
	short inHeight	= inHeader.height;
	size_t inDataSize = static_cast<size_t>(bytesPerPixel * inWidth * inHeight);

	short outWidth	 = inWidth >> 1;
	short outHeight	= inHeight >> 1;
	size_t outDataSize = bytesPerPixel * outWidth * outHeight;

	// Allocate all needed memory at once
	BYTE* allocatedBuffer = new BYTE[inDataSize + outDataSize];

	BYTE* inData = allocatedBuffer;

	switch(inHeader.datatypecode)
	{
	case 2: // 24 Bit
	case 3: // 32 Bit
		if(false == GetImageDataRaw(in, inData, inDataSize))
		{
			delete[] allocatedBuffer;
			fprintf_s(stderr, "Failed to read raw image data from \"%s\"\n", argv[1]);
			return 1;
		}
		break;
	case 10: // RLE Compression
		if(false == GetImageDataRLE(in, inData, inDataSize, bytesPerPixel))
		{
			delete[] allocatedBuffer;
			fprintf_s(stderr, "Failed to read RLE image data from \"%s\"\n", argv[1]);
			return 1;
		}
		break;
	default:
		delete[] allocatedBuffer;
		fprintf_s(stderr, "Unsupported image format\n");
		return 1;
	}

	in.close();

	printf_s("Done.\n");

	printf_s("Original size: %ix%i\n", inWidth, inHeight);
	printf_s("Resizing to: %ix%i\n", outWidth, outHeight);

	BYTE* outData = &allocatedBuffer[inDataSize];

	ResizeImage(outData, outWidth, outHeight, inData, inWidth, inHeight, bytesPerPixel);

	printf_s("Done.\n");

	printf_s("Saving \"%s\"...\n", argv[2]);

	std::ofstream output(argv[2], std::ios::binary);

	TGA_Header outHeader = inHeader;
	// Outout header is the same expect for the dimensions
	outHeader.width  = outWidth;
	outHeader.height = outHeight;

	output.write(reinterpret_cast<char*>(&outHeader), sizeof(TGA_Header));
	if(false == output.good())
	{
		delete[] allocatedBuffer;
		fprintf_s(stderr, "Failed to write header to file\n");
		return 1;
	}

	switch(outHeader.datatypecode)
	{
	case 2:
	case 3:
		if(false == WriteRAWToFile(output, outData, outDataSize))
		{
			bool o = output.eof();
			bool b = output.bad();
			bool f = output.fail();
			o = b = f = false;
			delete[] allocatedBuffer;
			fprintf_s(stderr, "Failed to write raw image to file\n");
			return 1;
		}
		break;
	case 10:
		if(false == WriteRLEToFile(output, outData, outDataSize, bytesPerPixel))
		{
			delete[] allocatedBuffer;
			fprintf_s(stderr, "Failed to write RLE image to file\n");
			return 1;
		}
		break;
	default: // This cant be reached, better tell the compiler!
		__assume(0);
	}
	output.close();

	printf_s("Done.\n");

	delete[] allocatedBuffer;
	return 0;
}

void ResizeImage(BYTE* outData,
				 short _outWidth,
				 short _outHeight,
				 BYTE* inData,
				 short _inWidth,
				 short _inHeight,
				 char bpp)
{
	for(int y = 0; y < _outHeight; y++)
	{
		double normY = y / double(_outHeight);

		for(int x = 0; x < _outWidth; x++)
		{

			double normX = x / double(_outWidth);

			for(int channel = 0; channel < bpp; channel++)
			{
				*outData++ = BilinearFilteredAtChannel(
					inData, _inWidth, _inHeight, bpp, channel, normX, normY);
			}
		}
	}
}
// Taken from https://en.wikipedia.org/wiki/Bilinear_filtering
BYTE BilinearFilteredAtChannel(
	BYTE* tex, int texSizeX, int texSizeY, int bpp, int channel, double u, double v)
{
	u = clamp(u * texSizeX - 0.5, 0, texSizeX - 1);
	v = clamp(v * texSizeY - 0.5, 0, texSizeY - 1);

	int x = static_cast<int>(u);
	int y = static_cast<int>(v);

	double u_ratio = u - x;
	double v_ratio = v - y;

	double u_opposite = 1 - u_ratio;
	double v_opposite = 1 - v_ratio;

	double c00 = static_cast<double>(tex[((x + (y * texSizeX)) * bpp) + channel]);
	double c01 = static_cast<double>(tex[(((x + 1) + (y * texSizeX)) * bpp) + channel]);
	double c10 = static_cast<double>(tex[((x + ((y + 1) * texSizeX)) * bpp) + channel]);
	double c11 = static_cast<double>(tex[(((x + 1) + ((y + 1) * texSizeX)) * bpp) + channel]);

	BYTE result = static_cast<BYTE>((c00 * u_opposite + c01 * u_ratio) * v_opposite +
									(c10 * u_opposite + c11 * u_ratio) * v_ratio);
	return result;
}

bool GetTGAHeader(std::ifstream& _file, TGA_Header* _header)
{
	_file.read(reinterpret_cast<char*>(_header), sizeof(TGA_Header));

	return _file.good();
}

bool GetImageDataRaw(std::ifstream& _file, BYTE* _data, size_t _sizeInBytes)
{
	_file.read(reinterpret_cast<char*>(_data), _sizeInBytes);
	return _file.good();
}

bool GetImageDataRLE(std::ifstream& _file, BYTE* _data, size_t _sizeInBytes, char _BytesPerPixel)
{
	size_t byteCounter = 0;
	while(byteCounter < _sizeInBytes)
	{
		BYTE temp[5];
		// 0 is for the runlength header
		// 1-4 is a color value

		_file.read(reinterpret_cast<char*>(temp), _BytesPerPixel + 1);
		if(false == _file.good())
			return false;

		for(int u = 0; u < _BytesPerPixel; u++)
		{
			*_data++ = temp[1 + u];
		}
		byteCounter += _BytesPerPixel;

		int j = temp[0] & 0x7f;

		if(temp[0] & 0x80) // Is the RLE bit set?
		{
			// RLE chunk
			for(int i = 0; i < j; i++)
			{
				for(int u = 0; u < _BytesPerPixel; u++)
				{
					*_data++ = temp[1 + u];
				}
				byteCounter += _BytesPerPixel;
			}
		}
		else
		{
			// Raw chunk
			for(int i = 0; i < j; i++)
			{
				_file.read(reinterpret_cast<char*>(temp), _BytesPerPixel);
				if(false == _file.good())
					return false;

				for(int u = 0; u < _BytesPerPixel; u++)
				{
					*_data++ = temp[u];
				}
				byteCounter += _BytesPerPixel;
			}
		}
	}
	return true;
}

bool WriteRAWToFile(std::ofstream& _file, BYTE* _data, size_t _sizeInBytes)
{

	_file.write(reinterpret_cast<char*>(_data), _sizeInBytes);

	return _file.good();
}

bool WriteRLEToFile(std::ofstream& _file, BYTE* _data, size_t _sizeInBytes, char _bytePerPixel)
{
	const unsigned char CHUNK_SIZE = 128;

	size_t totalPixels  = _sizeInBytes / _bytePerPixel;
	size_t currentPixel = 0;
	while(currentPixel < totalPixels)
	{
		size_t chunkOffset = currentPixel * _bytePerPixel;
		size_t currentByte = chunkOffset;

		unsigned char runLength = 1;
		bool raw				= true;

		while(currentPixel + runLength < totalPixels && runLength < CHUNK_SIZE)
		{
			bool nextColorSame = true;
			for(int channel = 0; nextColorSame && channel < _bytePerPixel; channel++)
			{
				nextColorSame =
					(_data[currentByte + channel] == _data[currentByte + channel + _bytePerPixel]);
			}

			currentByte += _bytePerPixel;
			if(1 == runLength)
			{ // Set the initial state, are we in raw or RLE mode
				raw = !nextColorSame;
			}
			else
			{
				if(raw && nextColorSame)
				{ // Found same pixel, but we are in raw mode so we will move back and get it next run
					runLength--;
					break;
				}
				if(!raw && !nextColorSame)
				{ // Our color neighbours ran out
					break;
				}
			}

			runLength++;
		}

		currentPixel += runLength;
		// Raw have its counter in the 0-127 and RLE in 128-255
		_file.put(raw ? runLength - 1 : runLength + 127);

		if(false == _file.good())
		{
			return false;
		}

		_file.write(reinterpret_cast<char*>(_data + chunkOffset),
					(raw ? runLength * _bytePerPixel : _bytePerPixel));

		if(false == _file.good())
		{
			return false;
		}
	}
	return true;
}
