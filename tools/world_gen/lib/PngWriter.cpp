// tools/world_gen — écriture PNG RGB 8 bits autonome (aucune dépendance zlib).
//
// Compression DEFLATE « codes de Huffman fixes » + LZ77 glouton (fenêtre 32 Ko,
// chaînes de hachage bornées) : ratio correct sur une carte aux aplats de
// couleurs, code court et déterministe. Filtre PNG « Sub » sur chaque ligne.
// Le décodeur côté client est stb_image (déjà embarqué), qui accepte tout
// flux zlib conforme RFC 1950/1951.

#include "PngWriter.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace tools::world_gen
{
	namespace
	{
		uint32_t Crc32(const uint8_t* data, size_t len, uint32_t crc = 0xFFFFFFFFu)
		{
			static std::array<uint32_t, 256> table = [] {
				std::array<uint32_t, 256> t{};
				for (uint32_t i = 0; i < 256u; ++i)
				{
					uint32_t c = i;
					for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : (c >> 1);
					t[i] = c;
				}
				return t;
			}();
			for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
			return crc;
		}

		class BitWriter
		{
		public:
			std::vector<uint8_t> out;
			/// Écrit `n` bits (LSB d'abord, ordre DEFLATE).
			void Bits(uint32_t v, int n)
			{
				for (int i = 0; i < n; ++i)
				{
					m_acc |= ((v >> i) & 1u) << m_count;
					if (++m_count == 8) { out.push_back(static_cast<uint8_t>(m_acc)); m_acc = 0; m_count = 0; }
				}
			}
			/// Écrit un code de Huffman (MSB d'abord, comme l'exige DEFLATE).
			void Code(uint32_t code, int len)
			{
				for (int i = len - 1; i >= 0; --i) Bits((code >> i) & 1u, 1);
			}
			void Flush() { if (m_count) { out.push_back(static_cast<uint8_t>(m_acc)); m_acc = 0; m_count = 0; } }
		private:
			uint32_t m_acc = 0;
			int m_count = 0;
		};

		void WriteLitLen(BitWriter& bw, int sym)
		{
			if (sym < 144) bw.Code(0x30u + static_cast<uint32_t>(sym), 8);
			else if (sym < 256) bw.Code(0x190u + static_cast<uint32_t>(sym - 144), 9);
			else if (sym < 280) bw.Code(static_cast<uint32_t>(sym - 256), 7);
			else bw.Code(0xC0u + static_cast<uint32_t>(sym - 280), 8);
		}

		constexpr int kLenBase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
		constexpr int kLenExtra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
		constexpr int kDistBase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
		constexpr int kDistExtra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

		void WriteMatch(BitWriter& bw, int len, int dist)
		{
			int li = 28;
			for (int i = 0; i < 29; ++i) if (kLenBase[i] > len) { li = i - 1; break; }
			if (len == 258) li = 28;
			WriteLitLen(bw, 257 + li);
			bw.Bits(static_cast<uint32_t>(len - kLenBase[li]), kLenExtra[li]);
			int di = 29;
			for (int i = 0; i < 30; ++i) if (kDistBase[i] > dist) { di = i - 1; break; }
			bw.Code(static_cast<uint32_t>(di), 5);
			bw.Bits(static_cast<uint32_t>(dist - kDistBase[di]), kDistExtra[di]);
		}

		std::vector<uint8_t> Deflate(const std::vector<uint8_t>& in)
		{
			BitWriter bw;
			bw.Bits(1, 1); // BFINAL
			bw.Bits(1, 2); // BTYPE = 01 (Huffman fixe)
			constexpr int kWindow = 32768, kHashBits = 15, kMaxChain = 48;
			std::vector<int> head(1u << kHashBits, -1), prev(in.size(), -1);
			auto hash3 = [&](size_t i) {
				return ((static_cast<uint32_t>(in[i]) << 10) ^ (static_cast<uint32_t>(in[i + 1]) << 5) ^ in[i + 2]) & ((1u << kHashBits) - 1u);
			};
			size_t i = 0;
			const size_t n = in.size();
			while (i < n)
			{
				int bestLen = 0, bestDist = 0;
				if (i + 2 < n)
				{
					const uint32_t h = hash3(i);
					int cand = head[h];
					int chain = 0;
					while (cand >= 0 && static_cast<int>(i) - cand <= kWindow && chain++ < kMaxChain)
					{
						int l = 0;
						const int maxL = static_cast<int>(std::min<size_t>(258, n - i));
						while (l < maxL && in[static_cast<size_t>(cand) + l] == in[i + l]) ++l;
						if (l > bestLen) { bestLen = l; bestDist = static_cast<int>(i) - cand; if (l == maxL) break; }
						cand = prev[static_cast<size_t>(cand)];
					}
					prev[i] = head[h];
					head[h] = static_cast<int>(i);
				}
				if (bestLen >= 3)
				{
					WriteMatch(bw, bestLen, bestDist);
					// Indexe les positions sautées (meilleures correspondances futures).
					for (int k = 1; k < bestLen && i + k + 2 < n; ++k)
					{
						const uint32_t h = hash3(i + k);
						prev[i + k] = head[h];
						head[h] = static_cast<int>(i + k);
					}
					i += static_cast<size_t>(bestLen);
				}
				else
				{
					WriteLitLen(bw, in[i]);
					++i;
				}
			}
			WriteLitLen(bw, 256); // fin de bloc
			bw.Flush();
			return bw.out;
		}

		void PutBE32(std::vector<uint8_t>& b, uint32_t v)
		{
			b.push_back(uint8_t(v >> 24)); b.push_back(uint8_t(v >> 16)); b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v));
		}

		void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
		{
			PutBE32(out, static_cast<uint32_t>(data.size()));
			const size_t start = out.size();
			out.insert(out.end(), type, type + 4);
			out.insert(out.end(), data.begin(), data.end());
			const uint32_t crc = Crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu;
			PutBE32(out, crc);
		}
	}

	std::vector<uint8_t> EncodePngRgb(const std::vector<uint8_t>& rgb, uint32_t width, uint32_t height)
	{
		// Lignes filtrées « Sub » : octet de filtre 1 puis différences horizontales.
		std::vector<uint8_t> raw;
		raw.reserve(static_cast<size_t>(height) * (width * 3u + 1u));
		for (uint32_t y = 0; y < height; ++y)
		{
			raw.push_back(1u);
			const uint8_t* row = rgb.data() + static_cast<size_t>(y) * width * 3u;
			for (uint32_t x = 0; x < width * 3u; ++x)
				raw.push_back(static_cast<uint8_t>(row[x] - (x >= 3 ? row[x - 3] : 0)));
		}
		const std::vector<uint8_t> def = Deflate(raw);
		std::vector<uint8_t> z(def.size() + 2u);
		z[0] = 0x78; z[1] = 0x01; // zlib : deflate, fenêtre 32 Ko, sans dictionnaire
		std::copy(def.begin(), def.end(), z.begin() + 2);
		uint32_t a = 1, b = 0;
		for (uint8_t c : raw) { a = (a + c) % 65521u; b = (b + a) % 65521u; }
		PutBE32(z, (b << 16) | a);

		std::vector<uint8_t> out = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
		std::vector<uint8_t> ihdr;
		PutBE32(ihdr, width); PutBE32(ihdr, height);
		ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
		Chunk(out, "IHDR", ihdr);
		Chunk(out, "IDAT", z);
		Chunk(out, "IEND", {});
		return out;
	}
}
