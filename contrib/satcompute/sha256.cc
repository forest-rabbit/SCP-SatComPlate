/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "sha256.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ns3
{

namespace
{

constexpr std::array<uint32_t, 64> ROUND_CONSTANTS = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

uint32_t
RotateRight(uint32_t value, uint32_t count)
{
    return (value >> count) | (value << (32 - count));
}

class Sha256
{
  public:
    Sha256()
        : m_state{0x6a09e667,
                  0xbb67ae85,
                  0x3c6ef372,
                  0xa54ff53a,
                  0x510e527f,
                  0x9b05688c,
                  0x1f83d9ab,
                  0x5be0cd19}
    {
    }

    void Update(const uint8_t* data, std::size_t size)
    {
        for (std::size_t index = 0; index < size; ++index)
        {
            m_block[m_blockSize++] = data[index];
            if (m_blockSize == m_block.size())
            {
                Transform();
                m_totalBits += 512;
                m_blockSize = 0;
            }
        }
    }

    std::string Finalize()
    {
        m_totalBits += static_cast<uint64_t>(m_blockSize) * 8;
        m_block[m_blockSize++] = 0x80;

        if (m_blockSize > 56)
        {
            while (m_blockSize < m_block.size())
            {
                m_block[m_blockSize++] = 0;
            }
            Transform();
            m_blockSize = 0;
        }
        while (m_blockSize < 56)
        {
            m_block[m_blockSize++] = 0;
        }
        for (int shift = 56; shift >= 0; shift -= 8)
        {
            m_block[m_blockSize++] = static_cast<uint8_t>(m_totalBits >> shift);
        }
        Transform();

        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (uint32_t value : m_state)
        {
            output << std::setw(8) << value;
        }
        return output.str();
    }

  private:
    void Transform()
    {
        std::array<uint32_t, 64> schedule{};
        for (std::size_t index = 0; index < 16; ++index)
        {
            const std::size_t offset = index * 4;
            schedule[index] = (static_cast<uint32_t>(m_block[offset]) << 24) |
                              (static_cast<uint32_t>(m_block[offset + 1]) << 16) |
                              (static_cast<uint32_t>(m_block[offset + 2]) << 8) |
                              static_cast<uint32_t>(m_block[offset + 3]);
        }
        for (std::size_t index = 16; index < schedule.size(); ++index)
        {
            const uint32_t s0 = RotateRight(schedule[index - 15], 7) ^
                                RotateRight(schedule[index - 15], 18) ^
                                (schedule[index - 15] >> 3);
            const uint32_t s1 = RotateRight(schedule[index - 2], 17) ^
                                RotateRight(schedule[index - 2], 19) ^
                                (schedule[index - 2] >> 10);
            schedule[index] = schedule[index - 16] + s0 + schedule[index - 7] + s1;
        }

        uint32_t a = m_state[0];
        uint32_t b = m_state[1];
        uint32_t c = m_state[2];
        uint32_t d = m_state[3];
        uint32_t e = m_state[4];
        uint32_t f = m_state[5];
        uint32_t g = m_state[6];
        uint32_t h = m_state[7];

        for (std::size_t index = 0; index < schedule.size(); ++index)
        {
            const uint32_t sigma1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            const uint32_t choice = (e & f) ^ ((~e) & g);
            const uint32_t temporary1 =
                h + sigma1 + choice + ROUND_CONSTANTS[index] + schedule[index];
            const uint32_t sigma0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temporary2 = sigma0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    std::array<uint32_t, 8> m_state;
    std::array<uint8_t, 64> m_block{};
    std::size_t m_blockSize{0};
    uint64_t m_totalBits{0};
};

} // namespace

std::string
Sha256Bytes(std::string_view bytes)
{
    Sha256 sha256;
    sha256.Update(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    return sha256.Finalize();
}

std::string
Sha256File(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        throw std::runtime_error("cannot open input for SHA-256: " + path.string());
    }

    Sha256 sha256;
    std::array<char, 64 * 1024> buffer{};
    while (input)
    {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0)
        {
            sha256.Update(reinterpret_cast<const uint8_t*>(buffer.data()),
                          static_cast<std::size_t>(count));
        }
    }
    if (!input.eof())
    {
        throw std::runtime_error("cannot read input for SHA-256: " + path.string());
    }
    return sha256.Finalize();
}

} // namespace ns3
