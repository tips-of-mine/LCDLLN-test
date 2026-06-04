// M101.1 — Implémentation de la sérialisation JSON déterministe.
//
// Parser et émetteur JSON internes (la lib `routine_graph` est pure : pas de
// dépendance à un parseur JSON tiers ni à engine_core/zone_builder). Le format
// émis est volontairement minimal et contrôlé, ce qui garantit le round-trip.

#include "src/shared/routine/RoutineSerialization.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace engine::routine::serialization
{
	namespace
	{
		// ---------------- Émission ----------------

		void EmitString(std::string& out, std::string_view s)
		{
			out.push_back('"');
			for (char c : s)
			{
				switch (c)
				{
					case '"':  out += "\\\""; break;
					case '\\': out += "\\\\"; break;
					case '\n': out += "\\n";  break;
					case '\r': out += "\\r";  break;
					case '\t': out += "\\t";  break;
					default:
						if (static_cast<unsigned char>(c) < 0x20)
						{
							char buf[8];
							std::snprintf(buf, sizeof(buf), "\\u%04x",
							              static_cast<unsigned>(static_cast<unsigned char>(c)));
							out += buf;
						}
						else
						{
							out.push_back(c);
						}
						break;
				}
			}
			out.push_back('"');
		}

		void EmitInt(std::string& out, long long v)
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%lld", v);
			out += buf;
		}

		void EmitFloat(std::string& out, float v)
		{
			// %.9g garantit le round-trip d'un float IEEE-754 32 bits. On
			// normalise le séparateur décimal en '.' au cas où une locale
			// exotique serait active (robustesse).
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
			for (char& c : buf)
			{
				if (c == ',') c = '.';
			}
			out += buf;
		}

		void EmitPropValue(std::string& out, const RoutineProperty& p)
		{
			switch (p.type)
			{
				case RoutineDataType::Bool:  out += (p.bValue ? "true" : "false"); break;
				case RoutineDataType::Int:   EmitInt(out, static_cast<long long>(p.iValue)); break;
				case RoutineDataType::Float: EmitFloat(out, p.fValue); break;
				case RoutineDataType::Vec3:
					out.push_back('[');
					EmitFloat(out, p.vValue.x); out.push_back(',');
					EmitFloat(out, p.vValue.y); out.push_back(',');
					EmitFloat(out, p.vValue.z);
					out.push_back(']');
					break;
				case RoutineDataType::String:
				case RoutineDataType::EntityRef:
					EmitString(out, p.sValue);
					break;
				case RoutineDataType::None:
				default:
					out += "null";
					break;
			}
		}

		// ---------------- Parsing ----------------

		struct JVal
		{
			enum class T { Null, Bool, Num, Str, Arr, Obj } t = T::Null;
			bool b = false;
			double num = 0.0;
			std::string str;
			std::vector<JVal> arr;
			std::vector<std::pair<std::string, JVal>> obj;

			const JVal* Find(std::string_view key) const
			{
				for (const auto& kv : obj)
				{
					if (kv.first == key) return &kv.second;
				}
				return nullptr;
			}
		};

		class Parser
		{
		public:
			Parser(std::string_view s, std::string& err) : m_s(s), m_err(err) {}

			bool Parse(JVal& out)
			{
				SkipWs();
				if (!ParseValue(out)) return false;
				SkipWs();
				if (m_pos != m_s.size())
				{
					Fail("contenu inattendu après la valeur racine");
					return false;
				}
				return true;
			}

		private:
			void Fail(const char* msg)
			{
				if (m_err.empty()) m_err = msg;
			}

			void SkipWs()
			{
				while (m_pos < m_s.size())
				{
					char c = m_s[m_pos];
					if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++m_pos;
					else break;
				}
			}

			bool Eof() const { return m_pos >= m_s.size(); }
			char Peek() const { return m_s[m_pos]; }

			bool Expect(char c)
			{
				if (Eof() || m_s[m_pos] != c) { Fail("caractère attendu manquant"); return false; }
				++m_pos;
				return true;
			}

			bool ParseValue(JVal& out)
			{
				SkipWs();
				if (Eof()) { Fail("fin de chaîne inattendue"); return false; }
				char c = Peek();
				switch (c)
				{
					case '{': return ParseObject(out);
					case '[': return ParseArray(out);
					case '"': out.t = JVal::T::Str; return ParseString(out.str);
					case 't': case 'f': return ParseBool(out);
					case 'n': return ParseNull(out);
					default:
						if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
						Fail("valeur JSON invalide");
						return false;
				}
			}

			bool ParseObject(JVal& out)
			{
				out.t = JVal::T::Obj;
				if (!Expect('{')) return false;
				SkipWs();
				if (!Eof() && Peek() == '}') { ++m_pos; return true; }
				for (;;)
				{
					SkipWs();
					if (Eof() || Peek() != '"') { Fail("clé d'objet attendue"); return false; }
					std::string key;
					if (!ParseString(key)) return false;
					SkipWs();
					if (!Expect(':')) return false;
					JVal v;
					if (!ParseValue(v)) return false;
					out.obj.emplace_back(std::move(key), std::move(v));
					SkipWs();
					if (Eof()) { Fail("objet non terminé"); return false; }
					char c = Peek();
					if (c == ',') { ++m_pos; continue; }
					if (c == '}') { ++m_pos; return true; }
					Fail("',' ou '}' attendu");
					return false;
				}
			}

			bool ParseArray(JVal& out)
			{
				out.t = JVal::T::Arr;
				if (!Expect('[')) return false;
				SkipWs();
				if (!Eof() && Peek() == ']') { ++m_pos; return true; }
				for (;;)
				{
					JVal v;
					if (!ParseValue(v)) return false;
					out.arr.push_back(std::move(v));
					SkipWs();
					if (Eof()) { Fail("tableau non terminé"); return false; }
					char c = Peek();
					if (c == ',') { ++m_pos; continue; }
					if (c == ']') { ++m_pos; return true; }
					Fail("',' ou ']' attendu");
					return false;
				}
			}

			bool ParseString(std::string& out)
			{
				if (!Expect('"')) return false;
				out.clear();
				while (!Eof())
				{
					char c = m_s[m_pos++];
					if (c == '"') return true;
					if (c == '\\')
					{
						if (Eof()) { Fail("échappement non terminé"); return false; }
						char e = m_s[m_pos++];
						switch (e)
						{
							case '"':  out.push_back('"');  break;
							case '\\': out.push_back('\\'); break;
							case '/':  out.push_back('/');  break;
							case 'n':  out.push_back('\n'); break;
							case 'r':  out.push_back('\r'); break;
							case 't':  out.push_back('\t'); break;
							case 'b':  out.push_back('\b'); break;
							case 'f':  out.push_back('\f'); break;
							case 'u':
							{
								if (m_pos + 4 > m_s.size()) { Fail("\\u incomplet"); return false; }
								unsigned code = 0;
								for (int i = 0; i < 4; ++i)
								{
									char h = m_s[m_pos++];
									code <<= 4;
									if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
									else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
									else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
									else { Fail("hex \\u invalide"); return false; }
								}
								// Encodage UTF-8 minimal (suffisant pour notre contenu).
								if (code < 0x80)
								{
									out.push_back(static_cast<char>(code));
								}
								else if (code < 0x800)
								{
									out.push_back(static_cast<char>(0xC0 | (code >> 6)));
									out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
								}
								else
								{
									out.push_back(static_cast<char>(0xE0 | (code >> 12)));
									out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
									out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
								}
								break;
							}
							default: Fail("séquence d'échappement inconnue"); return false;
						}
					}
					else
					{
						out.push_back(c);
					}
				}
				Fail("chaîne non terminée");
				return false;
			}

			bool ParseNumber(JVal& out)
			{
				size_t start = m_pos;
				if (!Eof() && Peek() == '-') ++m_pos;
				while (!Eof())
				{
					char c = Peek();
					if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
					    c == '+' || c == '-')
						++m_pos;
					else
						break;
				}
				if (m_pos == start) { Fail("nombre invalide"); return false; }
				std::string tok(m_s.substr(start, m_pos - start));
				out.t = JVal::T::Num;
				out.num = std::strtod(tok.c_str(), nullptr);
				return true;
			}

			bool ParseBool(JVal& out)
			{
				if (m_s.compare(m_pos, 4, "true") == 0) { m_pos += 4; out.t = JVal::T::Bool; out.b = true; return true; }
				if (m_s.compare(m_pos, 5, "false") == 0) { m_pos += 5; out.t = JVal::T::Bool; out.b = false; return true; }
				Fail("booléen invalide");
				return false;
			}

			bool ParseNull(JVal& out)
			{
				if (m_s.compare(m_pos, 4, "null") == 0) { m_pos += 4; out.t = JVal::T::Null; return true; }
				Fail("null invalide");
				return false;
			}

			std::string_view m_s;
			size_t m_pos = 0;
			std::string& m_err;
		};

		// Accès typés tolérants (renvoient un défaut si absent/mauvais type).
		bool AsString(const JVal* v, std::string& out)
		{
			if (!v || v->t != JVal::T::Str) return false;
			out = v->str;
			return true;
		}
		bool AsNumber(const JVal* v, double& out)
		{
			if (!v || v->t != JVal::T::Num) return false;
			out = v->num;
			return true;
		}
	} // namespace

	std::string ToJson(const RoutineGraph& graph)
	{
		// Copies triées : nœuds par id, liens par id (canonicité).
		std::vector<RoutineNode> nodes = graph.nodes;
		std::vector<RoutineLink> links = graph.links;
		std::sort(nodes.begin(), nodes.end(),
		          [](const RoutineNode& a, const RoutineNode& b) { return a.id < b.id; });
		std::sort(links.begin(), links.end(),
		          [](const RoutineLink& a, const RoutineLink& b) { return a.id < b.id; });

		std::string out;
		out.reserve(256 + nodes.size() * 128);
		out.push_back('{');
		out += "\"version\":";
		EmitInt(out, static_cast<long long>(graph.version));
		out += ",\"kind\":";
		EmitString(out, ToString(graph.kind));
		out += ",\"name\":";
		EmitString(out, graph.name);

		out += ",\"nodes\":[";
		for (size_t i = 0; i < nodes.size(); ++i)
		{
			const RoutineNode& n = nodes[i];
			if (i) out.push_back(',');
			out.push_back('{');
			out += "\"id\":";   EmitInt(out, static_cast<long long>(n.id));
			out += ",\"type\":"; EmitString(out, ToString(n.type));
			out += ",\"x\":";   EmitFloat(out, n.canvasX);
			out += ",\"y\":";   EmitFloat(out, n.canvasY);
			out += ",\"pins\":[";
			for (size_t j = 0; j < n.pins.size(); ++j)
			{
				const RoutinePin& p = n.pins[j];
				if (j) out.push_back(',');
				out.push_back('{');
				out += "\"id\":";    EmitInt(out, static_cast<long long>(p.id));
				out += ",\"dir\":";  EmitString(out, ToString(p.direction));
				out += ",\"kind\":"; EmitString(out, ToString(p.kind));
				out += ",\"data\":"; EmitString(out, ToString(p.dataType));
				out += ",\"name\":"; EmitString(out, p.name);
				out.push_back('}');
			}
			out += "],\"props\":[";
			for (size_t j = 0; j < n.properties.size(); ++j)
			{
				const RoutineProperty& p = n.properties[j];
				if (j) out.push_back(',');
				out.push_back('{');
				out += "\"key\":";    EmitString(out, p.key);
				out += ",\"type\":";  EmitString(out, ToString(p.type));
				out += ",\"value\":"; EmitPropValue(out, p);
				out.push_back('}');
			}
			out += "]}";
		}
		out += "],\"links\":[";
		for (size_t i = 0; i < links.size(); ++i)
		{
			const RoutineLink& l = links[i];
			if (i) out.push_back(',');
			out.push_back('{');
			out += "\"id\":";   EmitInt(out, static_cast<long long>(l.id));
			out += ",\"from\":["; EmitInt(out, l.fromNodeId); out.push_back(','); EmitInt(out, l.fromPinId); out.push_back(']');
			out += ",\"to\":[";   EmitInt(out, l.toNodeId);   out.push_back(','); EmitInt(out, l.toPinId);   out.push_back(']');
			out.push_back('}');
		}
		out += "]}";
		return out;
	}

	std::optional<RoutineGraph> FromJson(std::string_view json, ParseError& outError)
	{
		std::string err;
		JVal root;
		Parser parser(json, err);
		if (!parser.Parse(root) || root.t != JVal::T::Obj)
		{
			outError.message = err.empty() ? "JSON racine invalide (objet attendu)" : err;
			return std::nullopt;
		}

		RoutineGraph g;

		double ver = 0.0;
		if (!AsNumber(root.Find("version"), ver))
		{
			outError.message = "champ 'version' manquant";
			return std::nullopt;
		}
		g.version = static_cast<uint32_t>(ver);
		if (g.version > kRoutineGraphVersion)
		{
			outError.message = "version de graphe non supportée (trop récente)";
			return std::nullopt;
		}

		std::string kindStr;
		if (!AsString(root.Find("kind"), kindStr) || !FromString(kindStr, g.kind))
		{
			outError.message = "champ 'kind' invalide";
			return std::nullopt;
		}

		AsString(root.Find("name"), g.name); // optionnel

		const JVal* nodes = root.Find("nodes");
		if (nodes && nodes->t == JVal::T::Arr)
		{
			for (const JVal& jn : nodes->arr)
			{
				if (jn.t != JVal::T::Obj) { outError.message = "nœud non-objet"; return std::nullopt; }
				RoutineNode n;
				double idv = 0.0;
				if (!AsNumber(jn.Find("id"), idv)) { outError.message = "nœud sans 'id'"; return std::nullopt; }
				n.id = static_cast<uint32_t>(idv);
				std::string typeStr;
				if (!AsString(jn.Find("type"), typeStr) || !FromString(typeStr, n.type))
				{
					outError.message = "type de nœud inconnu : " + typeStr;
					return std::nullopt;
				}
				double xv = 0.0, yv = 0.0;
				AsNumber(jn.Find("x"), xv); n.canvasX = static_cast<float>(xv);
				AsNumber(jn.Find("y"), yv); n.canvasY = static_cast<float>(yv);

				const JVal* pins = jn.Find("pins");
				if (pins && pins->t == JVal::T::Arr)
				{
					for (const JVal& jp : pins->arr)
					{
						RoutinePin p;
						double pid = 0.0; AsNumber(jp.Find("id"), pid); p.id = static_cast<uint32_t>(pid);
						std::string dir, kind, data;
						AsString(jp.Find("dir"), dir);   FromString(dir, p.direction);
						AsString(jp.Find("kind"), kind); FromString(kind, p.kind);
						AsString(jp.Find("data"), data); FromString(data, p.dataType);
						AsString(jp.Find("name"), p.name);
						n.pins.push_back(std::move(p));
					}
				}

				const JVal* props = jn.Find("props");
				if (props && props->t == JVal::T::Arr)
				{
					for (const JVal& jpr : props->arr)
					{
						RoutineProperty pr;
						AsString(jpr.Find("key"), pr.key);
						std::string typeS;
						AsString(jpr.Find("type"), typeS); FromString(typeS, pr.type);
						const JVal* val = jpr.Find("value");
						if (val)
						{
							switch (pr.type)
							{
								case RoutineDataType::Bool:
									pr.bValue = (val->t == JVal::T::Bool) ? val->b : false; break;
								case RoutineDataType::Int:
									pr.iValue = (val->t == JVal::T::Num) ? static_cast<int64_t>(val->num) : 0; break;
								case RoutineDataType::Float:
									pr.fValue = (val->t == JVal::T::Num) ? static_cast<float>(val->num) : 0.0f; break;
								case RoutineDataType::Vec3:
									if (val->t == JVal::T::Arr && val->arr.size() == 3)
									{
										pr.vValue.x = static_cast<float>(val->arr[0].num);
										pr.vValue.y = static_cast<float>(val->arr[1].num);
										pr.vValue.z = static_cast<float>(val->arr[2].num);
									}
									break;
								case RoutineDataType::String:
								case RoutineDataType::EntityRef:
									if (val->t == JVal::T::Str) pr.sValue = val->str; break;
								case RoutineDataType::None:
								default: break;
							}
						}
						n.properties.push_back(std::move(pr));
					}
				}
				g.nodes.push_back(std::move(n));
			}
		}

		const JVal* links = root.Find("links");
		if (links && links->t == JVal::T::Arr)
		{
			for (const JVal& jl : links->arr)
			{
				if (jl.t != JVal::T::Obj) { outError.message = "lien non-objet"; return std::nullopt; }
				RoutineLink l;
				double idv = 0.0; AsNumber(jl.Find("id"), idv); l.id = static_cast<uint32_t>(idv);
				const JVal* from = jl.Find("from");
				const JVal* to   = jl.Find("to");
				if (from && from->t == JVal::T::Arr && from->arr.size() == 2)
				{
					l.fromNodeId = static_cast<uint32_t>(from->arr[0].num);
					l.fromPinId  = static_cast<uint32_t>(from->arr[1].num);
				}
				if (to && to->t == JVal::T::Arr && to->arr.size() == 2)
				{
					l.toNodeId = static_cast<uint32_t>(to->arr[0].num);
					l.toPinId  = static_cast<uint32_t>(to->arr[1].num);
				}
				g.links.push_back(std::move(l));
			}
		}

		return g;
	}
}
