// Henrik Nilsson
/*
Logg:
Got resizing working

Sources:
http://www.paulbourke.net/dataformats/tga/

*/

#include "ScopedTimer.h"

#include <fstream>
#include <iostream>

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

static bool GetTGAHeader(std::ifstream& _file, TGA_Header* _header);

#pragma endregion

#pragma region IO

static bool GetImageDataRaw(std::ifstream& _file, BYTE* _data, size_t _sizeInBytes);
static bool GetImageDataRLE(std::ifstream& _file, BYTE* _data, char _BytesPerPixel);

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

	short inWidth	  = inHeader.width;
	short inHeight	 = inHeader.height;
	char bytesPerPixel = inHeader.bitsperpixel >> 3; // Convert to BytesPerPixel instead

	short outWidth  = inWidth >> 1;
	short outHeight = inHeight >> 1;

	unsigned long InputDataSize  = bytesPerPixel * inWidth * inHeight;
	unsigned long OutputDataSize = bytesPerPixel * outWidth * outHeight;

	// Allocate all needed memory at once
	BYTE* TotalData = new BYTE[InputDataSize + OutputDataSize];

	BYTE* inData = TotalData;

	switch(inHeader.datatypecode)
	{
	case 2: // 24 Bit
	case 3: // 32 Bit
		if(false == GetImageDataRaw(in, inData, InputDataSize))
		{
			delete[] TotalData;
			fprintf_s(stderr, "Failed to read raw image data from \"%s\"\n", argv[1]);
			return 1;
		}
		break;
	case 10: // RLE Compression
		if(false == GetImageDataRLE(in, inData, bytesPerPixel))
		{
			delete[] TotalData;
			fprintf_s(stderr, "Failed to read RLE image data from \"%s\"\n", argv[1]);
			return 1;
		}
		break;
	default:
		delete[] TotalData;
		fprintf_s(stderr, "Unsupported image format\n");
		return 1;
	}

	in.close();
	printf_s("Done.\n");

	printf_s("Original size: %ix%i\n", inWidth, inHeight);
	printf_s("Resizing to: %ix%i\n", outWidth, outHeight);

	BYTE* outData = &TotalData[InputDataSize];

	ResizeImage(outData, outWidth, outHeight, inData, inWidth, inHeight, bytesPerPixel);

	printf_s("Done.\n");

	printf_s("Saving \"%s\"...\n", argv[2]);
	std::ofstream output(argv[2], std::ios::binary);
	TGA_Header outHeader = {};

	outHeader.bitsperpixel = inHeader.bitsperpixel;
	outHeader.width		   = outWidth;
	outHeader.height	   = outHeight;
	outHeader.datatypecode = inHeader.datatypecode;

	output.write(reinterpret_cast<char*>(&outHeader), sizeof(inHeader));

	switch(outHeader.datatypecode)
	{
	case 2:
	case 3:
		if(false == WriteRAWToFile(output, outData, OutputDataSize))
		{
			delete[] TotalData;
			fprintf_s(stderr, "Failed to write raw image to file\n");
			return 1;
		}
		break;
	case 10:
		if(false == WriteRLEToFile(output, outData, OutputDataSize, bytesPerPixel))
		{
			delete[] TotalData;
			fprintf_s(stderr, "Failed to write RLE image to file\n");
			return 1;
		}
		break;
	default: // This cant be reached, better tell the compiler!
		__assume(0);
	}
	output.close();

	printf_s("Done.\n");

	delete[] TotalData;
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
	// Resizing with no filtering
	ScopedTimer timer("Billinear filtering");
	for(int y = 0; y < _outHeight; y++)
	{
		float normY = y / float(_outHeight);
		for(int x = 0; x < _outWidth; x++)
		{

			float normX = x / float(_outWidth);
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

	u = min(max(u * texSizeX - 0.5, 0), texSizeX);
	v = max(0, v * texSizeY - 0.5);

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

bool GetImageDataRLE(std::ifstream& _file, BYTE* _data, char _BytesPerPixel)
{
	while(false == _file.eof())
	{
		char temp[5];
		_file.read(temp, _BytesPerPixel + 1);
		int j = temp[0] & 0x7f;
		for(int u = 0; u < _BytesPerPixel; u++)
		{
			*_data++ = temp[1 + u];
		}

		if(temp[0] & 0x80)
		{
			// RLE chunk
			for(int i = 0; i < j; i++)
			{
				for(int u = 0; u < _BytesPerPixel; u++)
				{
					*_data++ = temp[1 + u];
				}
			}
		}
		else
		{
			for(int i = 0; i < j; i++)
			{
				_file.read(temp, _BytesPerPixel);
				for(int u = 0; u < _BytesPerPixel; u++)
				{
					*_data++ = temp[u];
				}
			}
		}
	}
	return true;
}

bool WriteRAWToFile(std::ofstream& _file, BYTE* _data, size_t _sizeInBytes)
{
	_file.write(reinterpret_cast<char*>(&_data), _sizeInBytes);
	return _file.good();
}

bool WriteRLEToFile(std::ofstream& _file, BYTE* _data, size_t _sizeInBytes, char _bytePerPixel)
{
	const unsigned char max_chunk_length = 128;
	unsigned long npixels				 = _sizeInBytes / _bytePerPixel;
	unsigned long curpix				 = 0;
	while(curpix < npixels)
	{
		unsigned long chunkstart = curpix * _bytePerPixel;
		unsigned long curbyte	= curpix * _bytePerPixel;
		unsigned char run_length = 1;
		bool raw				 = true;

		while(curpix + run_length < npixels && run_length < max_chunk_length)
		{
			bool succ_eq = true;
			for(int channel = 0; succ_eq && channel < _bytePerPixel; channel++)
			{
				succ_eq = (_data[curbyte + channel] == _data[curbyte + channel + _bytePerPixel]);
			}

			curbyte += _bytePerPixel;
			if(1 == run_length)
			{
				raw = !succ_eq;
			}
			if(raw && succ_eq)
			{
				run_length--;
				break;
			}
			if(!raw && !succ_eq)
			{
				break;
			}
			run_length++;
		}
		curpix += run_length;

		_file.put(raw ? run_length - 1 : run_length + 127);
		if(false == _file.good())
		{
			fprintf_s(stderr, "Failed to write RLE image\n");
			return false;
		}

		_file.write(reinterpret_cast<char*>(_data + chunkstart),
					(raw ? run_length * _bytePerPixel : _bytePerPixel));
		if(false == _file.good())
		{
			fprintf_s(stderr, "Failed to write RLE image\n");
			return false;
		}
	}
	return true;
}
