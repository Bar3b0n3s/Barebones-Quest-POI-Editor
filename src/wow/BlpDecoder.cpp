#include "wow/BlpDecoder.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace qpe
{
namespace
{
std::uint32_t U32(std::span<std::uint8_t const> data, std::size_t at)
{
    if (at + 4 > data.size())
        return 0;
    return static_cast<std::uint32_t>(data[at]) |
        (static_cast<std::uint32_t>(data[at + 1]) << 8) |
        (static_cast<std::uint32_t>(data[at + 2]) << 16) |
        (static_cast<std::uint32_t>(data[at + 3]) << 24);
}

std::uint16_t U16(std::uint8_t const* data)
{
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

std::array<std::uint8_t, 4> Rgb565(std::uint16_t value)
{
    auto const r = static_cast<std::uint8_t>((value >> 11) & 31);
    auto const g = static_cast<std::uint8_t>((value >> 5) & 63);
    auto const b = static_cast<std::uint8_t>(value & 31);
    return { static_cast<std::uint8_t>((r << 3) | (r >> 2)),
             static_cast<std::uint8_t>((g << 2) | (g >> 4)),
             static_cast<std::uint8_t>((b << 3) | (b >> 2)), 255 };
}

std::array<std::array<std::uint8_t, 4>, 4> ColorTable(std::uint8_t const* block, bool forceFourColor)
{
    auto const c0 = U16(block);
    auto const c1 = U16(block + 2);
    std::array<std::array<std::uint8_t, 4>, 4> colors { Rgb565(c0), Rgb565(c1), {}, {} };
    if (c0 > c1 || forceFourColor)
    {
        for (int component = 0; component < 3; ++component)
        {
            colors[2][component] = static_cast<std::uint8_t>((2 * colors[0][component] + colors[1][component]) / 3);
            colors[3][component] = static_cast<std::uint8_t>((colors[0][component] + 2 * colors[1][component]) / 3);
        }
        colors[2][3] = colors[3][3] = 255;
    }
    else
    {
        for (int component = 0; component < 3; ++component)
            colors[2][component] = static_cast<std::uint8_t>((colors[0][component] + colors[1][component]) / 2);
        colors[2][3] = 255;
        colors[3] = { 0, 0, 0, 0 };
    }
    return colors;
}

void PutPixel(RgbaImage& image, std::uint32_t x, std::uint32_t y, std::array<std::uint8_t, 4> const& rgba)
{
    if (x >= image.width || y >= image.height)
        return;
    auto const at = (static_cast<std::size_t>(y) * image.width + x) * 4;
    std::copy(rgba.begin(), rgba.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(at));
}

bool DecodeDxt(std::span<std::uint8_t const> data, std::uint8_t format, RgbaImage& image)
{
    auto const blockBytes = format == 0 ? 8u : 16u; // 0=Dxt1, 1=Dxt3, 7=Dxt5
    auto const blocksWide = (image.width + 3) / 4;
    auto const blocksHigh = (image.height + 3) / 4;
    if (data.size() < static_cast<std::size_t>(blocksWide) * blocksHigh * blockBytes)
        return false;
    std::size_t offset = 0;
    for (std::uint32_t by = 0; by < blocksHigh; ++by)
    {
        for (std::uint32_t bx = 0; bx < blocksWide; ++bx, offset += blockBytes)
        {
            auto const* block = data.data() + offset;
            std::array<std::uint8_t, 16> alpha {};
            std::uint8_t const* colorBlock = block;
            if (format == 1)
            {
                for (int i = 0; i < 16; ++i)
                {
                    auto const nibble = static_cast<std::uint8_t>((block[i / 2] >> ((i & 1) * 4)) & 0x0f);
                    alpha[i] = static_cast<std::uint8_t>(nibble * 17);
                }
                colorBlock += 8;
            }
            else if (format == 7)
            {
                std::array<std::uint8_t, 8> table {};
                table[0] = block[0]; table[1] = block[1];
                if (table[0] > table[1])
                    for (int i = 1; i <= 6; ++i)
                        table[i + 1] = static_cast<std::uint8_t>(((7 - i) * table[0] + i * table[1]) / 7);
                else
                {
                    for (int i = 1; i <= 4; ++i)
                        table[i + 1] = static_cast<std::uint8_t>(((5 - i) * table[0] + i * table[1]) / 5);
                    table[6] = 0; table[7] = 255;
                }
                std::uint64_t bits = 0;
                for (int i = 0; i < 6; ++i)
                    bits |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
                for (int i = 0; i < 16; ++i)
                    alpha[i] = table[(bits >> (3 * i)) & 7];
                colorBlock += 8;
            }
            else
                alpha.fill(255);

            auto colors = ColorTable(colorBlock, format != 0);
            auto const indices = static_cast<std::uint32_t>(colorBlock[4]) |
                (static_cast<std::uint32_t>(colorBlock[5]) << 8) |
                (static_cast<std::uint32_t>(colorBlock[6]) << 16) |
                (static_cast<std::uint32_t>(colorBlock[7]) << 24);
            for (int pixel = 0; pixel < 16; ++pixel)
            {
                auto color = colors[(indices >> (pixel * 2)) & 3];
                if (format != 0)
                    color[3] = alpha[pixel];
                PutPixel(image, bx * 4 + pixel % 4, by * 4 + pixel / 4, color);
            }
        }
    }
    return true;
}

std::uint8_t ReadAlpha(std::span<std::uint8_t const> alpha, std::uint8_t depth, std::size_t pixel)
{
    switch (depth)
    {
        case 0: return 255;
        case 1: return pixel / 8 < alpha.size() && (alpha[pixel / 8] & (1u << (pixel & 7))) ? 255 : 0;
        case 4: return pixel / 2 < alpha.size() ? static_cast<std::uint8_t>(((alpha[pixel / 2] >> ((pixel & 1) * 4)) & 15) * 17) : 255;
        case 8: return pixel < alpha.size() ? alpha[pixel] : 255;
        default: return 255;
    }
}

bool SaneDimensions(std::uint32_t width, std::uint32_t height)
{
    return width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
        static_cast<std::uint64_t>(width) * height <= 64ull * 1024 * 1024;
}
}

RgbaImage DecodeBlp(std::span<std::uint8_t const> bytes, std::string& error)
{
    error.clear();
    if (bytes.size() < 148 || (std::memcmp(bytes.data(), "BLP1", 4) != 0 && std::memcmp(bytes.data(), "BLP2", 4) != 0))
    {
        error = "Not a supported BLP1/BLP2 file";
        return {};
    }

    auto const blp2 = bytes[3] == '2';
    std::uint32_t width = 0, height = 0, mipOffset = 0, mipSize = 0;
    std::uint8_t alphaDepth = 0, alphaEncoding = 0, encoding = 0;
    std::size_t paletteOffset = 0;
    if (blp2)
    {
        encoding = bytes[8];
        alphaDepth = bytes[9];
        alphaEncoding = bytes[10];
        width = U32(bytes, 12); height = U32(bytes, 16);
        mipOffset = U32(bytes, 20); mipSize = U32(bytes, 84);
        paletteOffset = 148;
    }
    else
    {
        auto const compression = U32(bytes, 4);
        alphaDepth = static_cast<std::uint8_t>(U32(bytes, 8));
        width = U32(bytes, 12); height = U32(bytes, 16);
        mipOffset = U32(bytes, 20); mipSize = U32(bytes, 84);
        if (compression == 0)
        {
            error = "BLP1 JPEG compression is not used by standard 3.3.5a world-map tiles and is not supported";
            return {};
        }
        encoding = 1;
        paletteOffset = 148;
    }

    if (!SaneDimensions(width, height) || mipOffset > bytes.size() || mipSize > bytes.size() - mipOffset)
    {
        error = "BLP header or mipmap range is invalid";
        return {};
    }

    RgbaImage image { width, height };
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4);
    auto mip = bytes.subspan(mipOffset, mipSize);
    if (encoding == 2)
    {
        if (!DecodeDxt(mip, alphaEncoding, image))
        {
            error = "Truncated or unsupported DXT-compressed BLP mipmap";
            return {};
        }
        return image;
    }
    if (encoding != 1 || paletteOffset + 1024 > bytes.size())
    {
        error = "Unsupported BLP encoding";
        return {};
    }

    auto const pixelCount = static_cast<std::size_t>(width) * height;
    if (mip.size() < pixelCount)
    {
        error = "Truncated paletted BLP mipmap";
        return {};
    }
    auto const alpha = mip.subspan(pixelCount);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        auto const palette = paletteOffset + static_cast<std::size_t>(mip[pixel]) * 4;
        image.pixels[pixel * 4 + 0] = bytes[palette + 2];
        image.pixels[pixel * 4 + 1] = bytes[palette + 1];
        image.pixels[pixel * 4 + 2] = bytes[palette + 0];
        image.pixels[pixel * 4 + 3] = ReadAlpha(alpha, alphaDepth, pixel);
    }
    return image;
}
}
