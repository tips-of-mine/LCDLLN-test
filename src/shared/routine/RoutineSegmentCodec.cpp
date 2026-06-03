// M101.3 — Implémentation du codec `routines.bin`.

#include "src/shared/routine/RoutineSegmentCodec.h"

#include "src/shared/routine/RoutineSerialization.h"

namespace engine::routine::codec
{
	namespace
	{
		void WriteU32LE(std::vector<uint8_t>& out, uint32_t v)
		{
			out.push_back(static_cast<uint8_t>(v & 0xFFu));
			out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
			out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
			out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
		}

		bool ReadU32LE(const std::vector<uint8_t>& in, size_t& pos, uint32_t& out)
		{
			if (pos + 4 > in.size()) return false;
			out = static_cast<uint32_t>(in[pos]) |
			      (static_cast<uint32_t>(in[pos + 1]) << 8) |
			      (static_cast<uint32_t>(in[pos + 2]) << 16) |
			      (static_cast<uint32_t>(in[pos + 3]) << 24);
			pos += 4;
			return true;
		}
	} // namespace

	std::vector<uint8_t> EncodeRoutinesBin(const std::vector<RoutineGraph>& graphs)
	{
		std::vector<uint8_t> out;
		WriteU32LE(out, kRoutinesMagic);
		WriteU32LE(out, kRoutinesContainerVersion);
		WriteU32LE(out, static_cast<uint32_t>(graphs.size()));
		for (const RoutineGraph& g : graphs)
		{
			const std::string json = serialization::ToJson(g);
			WriteU32LE(out, static_cast<uint32_t>(json.size()));
			out.insert(out.end(), json.begin(), json.end());
		}
		return out;
	}

	std::optional<std::vector<RoutineGraph>> DecodeRoutinesBin(
		const std::vector<uint8_t>& bytes, std::string& outError)
	{
		size_t pos = 0;
		uint32_t magic = 0, version = 0, count = 0;
		if (!ReadU32LE(bytes, pos, magic) || magic != kRoutinesMagic)
		{
			outError = "routines.bin : magic invalide";
			return std::nullopt;
		}
		if (!ReadU32LE(bytes, pos, version) || version > kRoutinesContainerVersion)
		{
			outError = "routines.bin : version de conteneur non supportee";
			return std::nullopt;
		}
		if (!ReadU32LE(bytes, pos, count))
		{
			outError = "routines.bin : compteur manquant";
			return std::nullopt;
		}

		std::vector<RoutineGraph> graphs;
		graphs.reserve(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			uint32_t jsonLen = 0;
			if (!ReadU32LE(bytes, pos, jsonLen) || pos + jsonLen > bytes.size())
			{
				outError = "routines.bin : longueur JSON invalide";
				return std::nullopt;
			}
			std::string json(reinterpret_cast<const char*>(bytes.data() + pos), jsonLen);
			pos += jsonLen;
			serialization::ParseError perr;
			auto g = serialization::FromJson(json, perr);
			if (!g)
			{
				outError = "routines.bin : graphe " + std::to_string(i) + " : " + perr.message;
				return std::nullopt;
			}
			graphs.push_back(std::move(*g));
		}
		return graphs;
	}
}
