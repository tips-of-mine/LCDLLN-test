// M100.18 — Implémentation bibliothèque végétale (parser JSON + règles).

#include "src/client/world/foliage/FoliageLibrary.h"

#include <cstdlib>
#include <string_view>
#include <utility>

namespace engine::world::foliage
{
	bool PassesRules(const FoliageRules& rules, float slopeDeg, float altMeters, int splatLayer)
	{
		if (slopeDeg > rules.slopeMaxDeg) return false;
		if (altMeters < rules.altMin || altMeters > rules.altMax) return false;
		if (!rules.splatLayers.empty())
		{
			bool found = false;
			for (int l : rules.splatLayers) if (l == splatLayer) { found = true; break; }
			if (!found) return false;
		}
		return true;
	}

	namespace
	{
		// Mini-DOM JSON (mêmes conventions que la sérialisation de routine).
		struct JVal
		{
			enum class T { Null, Bool, Num, Str, Arr, Obj } t = T::Null;
			bool b = false;
			double num = 0.0;
			std::string str;
			std::vector<JVal> arr;
			std::vector<std::pair<std::string, JVal>> obj;
			const JVal* Find(std::string_view k) const
			{
				for (const auto& kv : obj) if (kv.first == k) return &kv.second;
				return nullptr;
			}
		};

		class Parser
		{
		public:
			Parser(std::string_view s, std::string& e) : m_s(s), m_err(e) {}
			bool Parse(JVal& out) { SkipWs(); if (!Value(out)) return false; SkipWs(); return true; }
		private:
			void Fail(const char* m) { if (m_err.empty()) m_err = m; }
			void SkipWs() { while (m_pos < m_s.size()) { char c = m_s[m_pos]; if (c==' '||c=='\t'||c=='\n'||c=='\r') ++m_pos; else break; } }
			bool Eof() const { return m_pos >= m_s.size(); }
			char Peek() const { return m_s[m_pos]; }
			bool Expect(char c) { if (Eof()||m_s[m_pos]!=c){Fail("char attendu");return false;} ++m_pos; return true; }
			bool Value(JVal& o)
			{
				SkipWs(); if (Eof()){Fail("fin inattendue");return false;}
				char c = Peek();
				if (c=='{') return Object(o);
				if (c=='[') return Array(o);
				if (c=='"'){ o.t=JVal::T::Str; return String(o.str); }
				if (c=='t'||c=='f') return Bool(o);
				if (c=='n') return Null(o);
				if (c=='-'||(c>='0'&&c<='9')) return Number(o);
				Fail("valeur invalide"); return false;
			}
			bool Object(JVal& o)
			{
				o.t=JVal::T::Obj; if(!Expect('{'))return false; SkipWs();
				if(!Eof()&&Peek()=='}'){++m_pos;return true;}
				for(;;){ SkipWs(); if(Eof()||Peek()!='"'){Fail("cle attendue");return false;}
					std::string k; if(!String(k))return false; SkipWs(); if(!Expect(':'))return false;
					JVal v; if(!Value(v))return false; o.obj.emplace_back(std::move(k),std::move(v)); SkipWs();
					if(Eof()){Fail("objet non termine");return false;} char c=Peek();
					if(c==','){++m_pos;continue;} if(c=='}'){++m_pos;return true;} Fail("',' ou '}'");return false; }
			}
			bool Array(JVal& o)
			{
				o.t=JVal::T::Arr; if(!Expect('['))return false; SkipWs();
				if(!Eof()&&Peek()==']'){++m_pos;return true;}
				for(;;){ JVal v; if(!Value(v))return false; o.arr.push_back(std::move(v)); SkipWs();
					if(Eof()){Fail("tableau non termine");return false;} char c=Peek();
					if(c==','){++m_pos;continue;} if(c==']'){++m_pos;return true;} Fail("',' ou ']'");return false; }
			}
			bool String(std::string& out)
			{
				if(!Expect('"'))return false; out.clear();
				while(!Eof()){ char c=m_s[m_pos++];
					if(c=='"')return true;
					if(c=='\\'){ if(Eof()){Fail("echappement");return false;} char e=m_s[m_pos++];
						switch(e){case '"':out.push_back('"');break;case '\\':out.push_back('\\');break;
							case '/':out.push_back('/');break;case 'n':out.push_back('\n');break;
							case 'r':out.push_back('\r');break;case 't':out.push_back('\t');break;
							case 'b':out.push_back('\b');break;case 'f':out.push_back('\f');break;
							default: out.push_back(e); break; } }
					else out.push_back(c); }
				Fail("chaine non terminee"); return false;
			}
			bool Number(JVal& o)
			{
				size_t s=m_pos; if(!Eof()&&Peek()=='-')++m_pos;
				while(!Eof()){char c=Peek(); if((c>='0'&&c<='9')||c=='.'||c=='e'||c=='E'||c=='+'||c=='-')++m_pos; else break;}
				if(m_pos==s){Fail("nombre invalide");return false;}
				o.t=JVal::T::Num; o.num=std::strtod(std::string(m_s.substr(s,m_pos-s)).c_str(),nullptr); return true;
			}
			bool Bool(JVal& o)
			{
				if(m_s.compare(m_pos,4,"true")==0){m_pos+=4;o.t=JVal::T::Bool;o.b=true;return true;}
				if(m_s.compare(m_pos,5,"false")==0){m_pos+=5;o.t=JVal::T::Bool;o.b=false;return true;}
				Fail("booleen invalide");return false;
			}
			bool Null(JVal& o)
			{
				if(m_s.compare(m_pos,4,"null")==0){m_pos+=4;o.t=JVal::T::Null;return true;}
				Fail("null invalide");return false;
			}
			std::string_view m_s; size_t m_pos=0; std::string& m_err;
		};
	} // namespace

	bool ParseFoliageLibraryJson(const std::string& jsonText, FoliageLibrary& out, std::string& err)
	{
		JVal root; Parser parser(jsonText, err);
		if (!parser.Parse(root) || root.t != JVal::T::Obj)
		{
			if (err.empty()) err = "library.json: racine objet attendue";
			return false;
		}
		if (const JVal* v = root.Find("version"); v && v->t == JVal::T::Num)
			out.version = static_cast<uint32_t>(v->num);

		if (const JVal* cats = root.Find("categories"); cats && cats->t == JVal::T::Arr)
		{
			for (const JVal& c : cats->arr)
			{
				if (c.t != JVal::T::Obj) continue;
				FoliageCategory cat;
				if (const JVal* id = c.Find("id"); id && id->t == JVal::T::Str) cat.id = id->str;
				if (const JVal* lb = c.Find("label"); lb && lb->t == JVal::T::Str) cat.label = lb->str;
				out.categories.push_back(std::move(cat));
			}
		}

		if (const JVal* assets = root.Find("assets"); assets && assets->t == JVal::T::Arr)
		{
			for (const JVal& a : assets->arr)
			{
				if (a.t != JVal::T::Obj) continue;
				FoliageAsset asset;
				if (const JVal* id = a.Find("id"); id && id->t == JVal::T::Str) asset.id = id->str;
				if (const JVal* cat = a.Find("category"); cat && cat->t == JVal::T::Str) asset.category = cat->str;
				if (const JVal* m = a.Find("mesh"); m && m->t == JVal::T::Str) asset.mesh = m->str;
				if (const JVal* bb = a.Find("billboard"); bb && bb->t == JVal::T::Bool) asset.billboard = bb->b;
				if (const JVal* r = a.Find("rules"); r && r->t == JVal::T::Obj)
				{
					if (const JVal* s = r->Find("slopeMaxDeg"); s && s->t == JVal::T::Num) asset.rules.slopeMaxDeg = static_cast<float>(s->num);
					if (const JVal* mn = r->Find("altMin"); mn && mn->t == JVal::T::Num) asset.rules.altMin = static_cast<float>(mn->num);
					if (const JVal* mx = r->Find("altMax"); mx && mx->t == JVal::T::Num) asset.rules.altMax = static_cast<float>(mx->num);
					if (const JVal* sl = r->Find("splatLayers"); sl && sl->t == JVal::T::Arr)
						for (const JVal& e : sl->arr) if (e.t == JVal::T::Num) asset.rules.splatLayers.push_back(static_cast<int>(e.num));
				}
				out.assets.push_back(std::move(asset));
			}
		}
		return true;
	}
}
