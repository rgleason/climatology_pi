#include "ClimatologyChecksum.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace climatology {
namespace {

const std::uint32_t kRound[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

std::uint32_t Rotate(std::uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32 - bits));
}

class Sha256State {
public:
    Sha256State()
        : m_hash{{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                  0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}},
          m_total(0), m_used(0) {}

    void Update(const unsigned char* data, std::size_t size)
    {
        m_total += size;
        while(size) {
            const std::size_t take = std::min(size, m_block.size() - m_used);
            std::copy(data, data + take, m_block.begin() + m_used);
            data += take;
            size -= take;
            m_used += take;
            if(m_used == m_block.size()) Transform(), m_used = 0;
        }
    }

    std::string Finish()
    {
        const std::uint64_t bits = m_total * 8;
        m_block[m_used++] = 0x80;
        if(m_used > 56) {
            std::fill(m_block.begin() + m_used, m_block.end(), 0);
            Transform();
            m_used = 0;
        }
        std::fill(m_block.begin() + m_used, m_block.begin() + 56, 0);
        for(int index = 0; index < 8; ++index)
            m_block[63 - index] = static_cast<unsigned char>(bits >> (index * 8));
        Transform();
        std::ostringstream text;
        text << std::hex << std::setfill('0');
        for(std::size_t i = 0; i < m_hash.size(); ++i)
            text << std::setw(8) << m_hash[i];
        return text.str();
    }

private:
    void Transform()
    {
        std::uint32_t words[64];
        for(int i = 0; i < 16; ++i)
            words[i] = static_cast<std::uint32_t>(m_block[i*4]) << 24 |
                       static_cast<std::uint32_t>(m_block[i*4+1]) << 16 |
                       static_cast<std::uint32_t>(m_block[i*4+2]) << 8 |
                       static_cast<std::uint32_t>(m_block[i*4+3]);
        for(int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = Rotate(words[i-15], 7) ^
                                     Rotate(words[i-15], 18) ^ (words[i-15] >> 3);
            const std::uint32_t s1 = Rotate(words[i-2], 17) ^
                                     Rotate(words[i-2], 19) ^ (words[i-2] >> 10);
            words[i] = words[i-16] + s0 + words[i-7] + s1;
        }
        std::uint32_t a=m_hash[0], b=m_hash[1], c=m_hash[2], d=m_hash[3];
        std::uint32_t e=m_hash[4], f=m_hash[5], g=m_hash[6], h=m_hash[7];
        for(int i = 0; i < 64; ++i) {
            const std::uint32_t sum1 = Rotate(e,6)^Rotate(e,11)^Rotate(e,25);
            const std::uint32_t choose = (e & f) ^ (~e & g);
            const std::uint32_t first = h + sum1 + choose + kRound[i] + words[i];
            const std::uint32_t sum0 = Rotate(a,2)^Rotate(a,13)^Rotate(a,22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t second = sum0 + majority;
            h=g; g=f; f=e; e=d+first; d=c; c=b; b=a; a=first+second;
        }
        m_hash[0]+=a; m_hash[1]+=b; m_hash[2]+=c; m_hash[3]+=d;
        m_hash[4]+=e; m_hash[5]+=f; m_hash[6]+=g; m_hash[7]+=h;
    }

    std::array<std::uint32_t, 8> m_hash;
    std::array<unsigned char, 64> m_block;
    std::uint64_t m_total;
    std::size_t m_used;
};

}  // namespace

std::string Sha256(const void* data, std::size_t size)
{
    Sha256State state;
    state.Update(static_cast<const unsigned char*>(data), size);
    return state.Finish();
}

std::string Sha256File(const std::string& path)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if(!file) return "";
    Sha256State state;
    std::array<unsigned char, 65536> buffer;
    while(file) {
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        const std::streamsize count = file.gcount();
        if(count > 0) state.Update(buffer.data(), static_cast<std::size_t>(count));
    }
    return file.eof() ? state.Finish() : "";
}

}  // namespace climatology
