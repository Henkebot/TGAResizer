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

static unsigned char getBilinearFilteredPixelColor(
	unsigned char* tex, int texSizeX, int texSizeY, int bpp, int channel, double u, double v)
{
	u = max(0, u * texSizeX - 0.5);
	v = max(0, v * texSizeY - 0.5);

	int x = floor(u);
	int y = floor(v);

	double u_ratio = u - x;
	double v_ratio = v - y;

	double u_opposite = 1 - u_ratio;
	double v_opposite = 1 - v_ratio;

	unsigned char result = (tex[((x + (y * texSizeX)) * bpp) + channel] * u_opposite +
							tex[(((x + 1) + (y * texSizeX)) * bpp) + channel] * u_ratio) *
							   v_opposite +
						   (tex[((x + ((y + 1) * texSizeX)) * bpp) + channel] * u_opposite +
							tex[(((x + 1) + ((y + 1) * texSizeX)) * bpp) + channel] * u_ratio) *
							   v_ratio;
	return result;
}

static bool GetTGAHeader(std::ifstream& _file, TGA_Header* _header);
static bool GetImageDataRaw(std::ifstream& _file, unsigned char* _data, size_t _sizeInBytes);
static bool GetImageDataRLE(std::ifstream& _file, unsigned char* _data, char _BytesPerPixel);

void ResizeImage(unsigned char* outData,
				 short _outWidth,
				 short _outHeight,
				 unsigned char* inData,
				 short _inWidth,
				 short _inHeight,
				 char bpp);

int main(int argc, char** argv)
{
	printf("Reading \"%s\"...\n", argv[1]);
	std::ifstream in(argv[1], std::ios::binary);
	if(false == in.is_open())
	{
		return 1;
	}

	TGA_Header header;

	GetTGAHeader(in, &header);

	short width  = header.width;
	short height = header.height;
	char bpp	 = header.bitsperpixel >> 3; // Convert to BytesPerPixel instead

	short nWidth  = width>> 1;
	short nHeight = height >> 1;

	unsigned long nbytes  = bpp * width * height;
	unsigned long nnBytes = bpp * nWidth * nHeight;

	unsigned char* data = new unsigned char[nbytes + nnBytes];
	switch(header.datatypecode)
	{
	case 2:
	case 3:
		GetImageDataRaw(in, data, nbytes);
		break;
	case 10:
		GetImageDataRLE(in, data, bpp);
		break;
	default:
		return 1;
	}

	in.close();
	printf("Done.\n");
	printf("Original size: %ix%i\n", width, height);

	printf("Resizing to: %ix%i\n", nWidth, nHeight);

	unsigned char* newData = &data[nbytes];

	ResizeImage(newData, nWidth, nHeight, data, width, height, bpp);

	printf("Done.\n");

	printf("Saving \"%s\"...\n", argv[2]);
	std::ofstream output(argv[2], std::ios::binary);
	TGA_Header oHeader = {};

	oHeader.bitsperpixel	= header.bitsperpixel;
	oHeader.width			= nWidth;
	oHeader.height			= nHeight;
	oHeader.datatypecode	= 2;
	oHeader.imagedescriptor = 0; // flip horizontally

	output.write(reinterpret_cast<char*>(&oHeader), sizeof(header));
	output.write(reinterpret_cast<char*>(newData), nWidth * nHeight * bpp);
	printf("Done.\n");
	output.close();
	delete[] data;
	system("pause");
	return 0;
}

void ResizeImage(unsigned char* outData,
				 short _outWidth,
				 short _outHeight,
				 unsigned char* inData,
				 short _inWidth,
				 short _inHeight,
				 char bpp)
{
	// Resizing with no filtering
	ScopedTimer timer("Billinear filtering");
	for(int j = 0; j < _outHeight; j++)
	{
		for(int i = 0; i < _outWidth; i++)
		{

			float gy = j / float(_outHeight); // be careful to interpolate boundaries
			float gx = i / float(_outWidth); // be careful to interpolate boundaries
			for(int channel = 0; channel < bpp; channel++)
			{
				outData[((i + (j * _outWidth)) * bpp) + channel] = getBilinearFilteredPixelColor(
					inData, _inWidth, _inHeight, bpp, channel, gx, gy);
			}
		}
	}
}

bool GetTGAHeader(std::ifstream& _file, TGA_Header* _header)
{
	_file.read(reinterpret_cast<char*>(_header), sizeof(TGA_Header));

	return _file.good();
}

bool GetImageDataRaw(std::ifstream& _file, unsigned char* _data, size_t _sizeInBytes)
{
	_file.read(reinterpret_cast<char*>(_data), _sizeInBytes);
	return _file.good();
}

bool GetImageDataRLE(std::ifstream& _file, unsigned char* _data, char _BytesPerPixel)
{
	while(_file)
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