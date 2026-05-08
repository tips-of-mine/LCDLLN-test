#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace engine::core::util
{
	/// Sérialiseur/désérialiseur binaire little-endian avec curseurs read/write
	/// indépendants. Inspiré du pattern cmangos (réécrit, pas de port littéral).
	///
	/// Modèle d'usage :
	///   ByteBuffer buf;
	///   buf << uint32_t(42) << std::string_view("toto");
	///   uint32_t v; std::string s;
	///   buf >> v >> s;
	///
	/// Tout overflow de lecture (au-delà du WritePos) bascule un flag d'erreur
	/// permanent (cf. HasError()). Les écritures, elles, étendent dynamiquement
	/// le buffer interne — pas de notion d'overflow d'écriture.
	///
	/// Bit-packing : WriteBit/ReadBit gèrent un sub-buffer 8 bits accumulé. Les
	/// reads/writes octet-aligned doivent être encadrés par FlushBits()/un read
	/// après ReadBit jusqu'à frontière octet — sinon les états bit/byte se
	/// chevauchent. AppendArithmetic flush automatiquement.
	class ByteBuffer
	{
	public:
		ByteBuffer();
		explicit ByteBuffer(std::size_t reserve);

		// --- Écriture (octet-aligné) ---

		/// Écrit un type arithmétique ou enum en little-endian.
		template <typename T>
		ByteBuffer& operator<<(T value)
		{
			static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>,
				"ByteBuffer::operator<< only supports arithmetic/enum types");
			AppendArithmetic(value);
			return *this;
		}

		/// Écrit une string : uint16 longueur (octets) + bytes UTF-8 bruts.
		ByteBuffer& operator<<(std::string_view s);
		ByteBuffer& operator<<(const std::string& s) { return *this << std::string_view(s); }
		ByteBuffer& operator<<(const char* s) { return *this << std::string_view(s ? s : ""); }

		/// Append direct de bytes bruts (pas de préfixe de longueur). Flush les
		/// bits en cours d'écriture si nécessaire.
		void AppendBytes(const std::uint8_t* src, std::size_t count);

		// --- Lecture (octet-aligné) ---

		/// Lit un type arithmétique ou enum en little-endian. Set HasError() si overflow.
		template <typename T>
		ByteBuffer& operator>>(T& value)
		{
			static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>,
				"ByteBuffer::operator>> only supports arithmetic/enum types");
			ReadArithmetic(value);
			return *this;
		}

		/// Lit une string : uint16 longueur + bytes. Set HasError() si overflow ou
		/// longueur > kMaxStringLength.
		ByteBuffer& operator>>(std::string& out);

		/// Lit \a count bytes bruts dans \a dst. Set HasError() si overflow.
		bool ReadBytes(std::uint8_t* dst, std::size_t count);

		// --- Bit-level ---

		/// Écrit 1 bit. Les bits sont accumulés little-endian (LSB d'abord).
		void WriteBit(bool b);

		/// Pad zéros jusqu'à la prochaine frontière d'octet. No-op si déjà aligné.
		void FlushBits();

		/// Lit 1 bit. Set HasError() si overflow. Retourne false en cas d'erreur.
		bool ReadBit();

		/// Réinitialise l'état bit-level côté lecture (nouvelle séquence de bits).
		void AlignReadToByte() noexcept;

		// --- Curseurs et état ---

		std::size_t WritePos() const noexcept { return m_writePos; }
		std::size_t ReadPos() const noexcept { return m_readPos; }
		std::size_t Size() const noexcept { return m_writePos; }
		bool HasError() const noexcept { return m_error; }

		/// Repositionne le curseur de lecture. Pas de check de borne (le
		/// prochain read attrapera l'overflow). \a pos > WritePos est légal mais
		/// déclenchera HasError() au prochain read.
		void SeekRead(std::size_t pos) noexcept;

		/// Repositionne le curseur d'écriture (utile pour patcher une zone réservée
		/// type "longueur" calculée en fin de paquet). Étend le buffer si \a pos
		/// dépasse la taille actuelle.
		void SeekWrite(std::size_t pos);

		/// Vue sur les octets effectivement écrits [0, WritePos).
		std::span<const std::uint8_t> Data() const noexcept;

		/// Réinitialise complètement (écriture, lecture, erreur, bit state).
		void Clear() noexcept;

		/// Limite max sur la longueur des strings sérialisées (inclusive). Évite
		/// qu'un buffer corrompu déclenche une allocation géante côté lecteur.
		static constexpr std::uint16_t kMaxStringLength = 0xFFFFu;

	private:
		// Byteswap manuel sans dépendre de std::byteswap (C++23). Sur les
		// plateformes little-endian (x86, ARM linux/win standard), c'est un
		// no-op à la compilation.
		template <typename T>
		static T ToLittleEndian(T value) noexcept
		{
			if constexpr (std::endian::native == std::endian::little)
			{
				return value;
			}
			else
			{
				using U = std::make_unsigned_t<std::conditional_t<std::is_enum_v<T>, std::underlying_type_t<T>, T>>;
				U u;
				std::memcpy(&u, &value, sizeof(U));
				U swapped = 0;
				for (std::size_t i = 0; i < sizeof(U); ++i)
				{
					swapped |= ((u >> (i * 8)) & 0xFFu) << ((sizeof(U) - 1 - i) * 8);
				}
				T out;
				std::memcpy(&out, &swapped, sizeof(T));
				return out;
			}
		}

		template <typename T>
		void AppendArithmetic(T value)
		{
			// Tout write octet-aligné force d'abord le flush du buffer de bits,
			// sinon l'ordre logique des données serait incohérent à la relecture.
			if (m_bitWritePos != 0)
			{
				FlushBits();
			}
			const T le = ToLittleEndian(value);
			std::uint8_t raw[sizeof(T)];
			std::memcpy(raw, &le, sizeof(T));
			AppendBytesRaw(raw, sizeof(T));
		}

		template <typename T>
		void ReadArithmetic(T& value)
		{
			AlignReadToByte();
			if (m_readPos + sizeof(T) > m_writePos)
			{
				m_error = true;
				value = T{};
				return;
			}
			T raw{};
			std::memcpy(&raw, m_data.data() + m_readPos, sizeof(T));
			m_readPos += sizeof(T);
			value = ToLittleEndian(raw); // ToLE est sa propre inverse (swap idempotent)
		}

		// Helper interne sans flush de bits (utilisé par les méthodes qui ont
		// déjà fait le flush, et par FlushBits lui-même).
		void AppendBytesRaw(const std::uint8_t* src, std::size_t count);

		std::vector<std::uint8_t> m_data;
		std::size_t m_writePos = 0;
		std::size_t m_readPos = 0;
		bool m_error = false;

		std::uint8_t m_bitWriteBuf = 0;
		std::uint8_t m_bitWritePos = 0; // 0..7

		std::uint8_t m_bitReadBuf = 0;
		std::uint8_t m_bitReadPos = 8; // 8 = "pas de bits chargés"
	};
}
