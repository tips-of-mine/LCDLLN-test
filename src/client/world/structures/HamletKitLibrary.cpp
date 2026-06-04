// M100.31 — Parsing des kits d'hameau (parser JSON compact, même pattern que
// les autres bibliothèques data-driven du projet).

#include "src/client/world/structures/HamletKitLibrary.h"

#include <cstdlib>
#include <string_view>
#include <utility>

namespace engine::world::structures
{
	namespace
	{
		struct JVal
		{
			enum class T { Null, Bool, Num, Str, Arr, Obj } t = T::Null;
			bool b = false; double num = 0.0; std::string str;
			std::vector<JVal> arr; std::vector<std::pair<std::string, JVal>> obj;
			const JVal* Find(std::string_view k) const { for (const auto& kv : obj) if (kv.first == k) return &kv.second; return nullptr; }
		};

		class Parser
		{
		public:
			Parser(std::string_view s, std::string& e) : m_s(s), m_err(e) {}
			bool Parse(JVal& o) { SkipWs(); if (!Value(o)) return false; SkipWs(); return true; }
		private:
			void Fail(const char* m) { if (m_err.empty()) m_err = m; }
			void SkipWs() { while (m_pos < m_s.size()) { char c = m_s[m_pos]; if (c==' '||c=='\t'||c=='\n'||c=='\r') ++m_pos; else break; } }
			bool Eof() const { return m_pos >= m_s.size(); }
			char Peek() const { return m_s[m_pos]; }
			bool Expect(char c) { if (Eof()||m_s[m_pos]!=c){Fail("char");return false;} ++m_pos; return true; }
			bool Value(JVal& o)
			{
				SkipWs(); if (Eof()){Fail("eof");return false;} char c = Peek();
				if (c=='{') return Object(o); if (c=='[') return Array(o);
				if (c=='"'){ o.t=JVal::T::Str; return String(o.str); }
				if (c=='t'||c=='f') return Bool(o); if (c=='n') return Null(o);
				if (c=='-'||(c>='0'&&c<='9')) return Number(o);
				Fail("valeur"); return false;
			}
			bool Object(JVal& o)
			{
				o.t=JVal::T::Obj; if(!Expect('{'))return false; SkipWs();
				if(!Eof()&&Peek()=='}'){++m_pos;return true;}
				for(;;){ SkipWs(); if(Eof()||Peek()!='"'){Fail("cle");return false;}
					std::string k; if(!String(k))return false; SkipWs(); if(!Expect(':'))return false;
					JVal v; if(!Value(v))return false; o.obj.emplace_back(std::move(k),std::move(v)); SkipWs();
					if(Eof()){Fail("obj");return false;} char c=Peek();
					if(c==','){++m_pos;continue;} if(c=='}'){++m_pos;return true;} Fail("',}'");return false; }
			}
			bool Array(JVal& o)
			{
				o.t=JVal::T::Arr; if(!Expect('['))return false; SkipWs();
				if(!Eof()&&Peek()==']'){++m_pos;return true;}
				for(;;){ JVal v; if(!Value(v))return false; o.arr.push_back(std::move(v)); SkipWs();
					if(Eof()){Fail("arr");return false;} char c=Peek();
					if(c==','){++m_pos;continue;} if(c==']'){++m_pos;return true;} Fail("',]'");return false; }
			}
			bool String(std::string& out)
			{
				if(!Expect('"'))return false; out.clear();
				while(!Eof()){ char c=m_s[m_pos++]; if(c=='"')return true;
					if(c=='\\'){ if(Eof()){Fail("esc");return false;} char e=m_s[m_pos++];
						switch(e){case '"':out.push_back('"');break;case '\\':out.push_back('\\');break;case '/':out.push_back('/');break;
							case 'n':out.push_back('\n');break;case 'r':out.push_back('\r');break;case 't':out.push_back('\t');break;
							default:out.push_back(e);break;} }
					else out.push_back(c); }
				Fail("str"); return false;
			}
			bool Number(JVal& o)
			{
				size_t s=m_pos; if(!Eof()&&Peek()=='-')++m_pos;
				while(!Eof()){char c=Peek(); if((c>='0'&&c<='9')||c=='.'||c=='e'||c=='E'||c=='+'||c=='-')++m_pos; else break;}
				if(m_pos==s){Fail("num");return false;} o.t=JVal::T::Num; o.num=std::strtod(std::string(m_s.substr(s,m_pos-s)).c_str(),nullptr); return true;
			}
			bool Bool(JVal& o){ if(m_s.compare(m_pos,4,"true")==0){m_pos+=4;o.t=JVal::T::Bool;o.b=true;return true;} if(m_s.compare(m_pos,5,"false")==0){m_pos+=5;o.t=JVal::T::Bool;o.b=false;return true;} Fail("bool");return false; }
			bool Null(JVal& o){ if(m_s.compare(m_pos,4,"null")==0){m_pos+=4;o.t=JVal::T::Null;return true;} Fail("null");return false; }
			std::string_view m_s; size_t m_pos=0; std::string& m_err;
		};
	} // namespace

	bool ParseHamletKitJson(const std::string& jsonText, HamletKit& out, std::string& err)
	{
		JVal root; Parser parser(jsonText, err);
		if (!parser.Parse(root) || root.t != JVal::T::Obj) { if (err.empty()) err = "kit: racine objet attendue"; return false; }
		if (const JVal* r = root.Find("race"); r && r->t == JVal::T::Str) out.race = r->str;
		if (const JVal* b = root.Find("biome"); b && b->t == JVal::T::Str) out.biome = b->str;
		if (const JVal* m = root.Find("minSpacingDefault"); m && m->t == JVal::T::Num) out.minSpacingDefault = static_cast<float>(m->num);
		if (const JVal* f = root.Find("footprintRadius"); f && f->t == JVal::T::Num) out.footprintRadius = static_cast<float>(f->num);
		if (const JVal* hs = root.Find("houses"); hs && hs->t == JVal::T::Arr)
		{
			for (const JVal& h : hs->arr)
			{
				if (h.t != JVal::T::Obj) continue;
				std::string mesh; float weight = 1.0f;
				if (const JVal* m = h.Find("mesh"); m && m->t == JVal::T::Str) mesh = m->str;
				if (const JVal* w = h.Find("weight"); w && w->t == JVal::T::Num) weight = static_cast<float>(w->num);
				out.houses.emplace_back(std::move(mesh), weight);
			}
		}
		return true;
	}
}
