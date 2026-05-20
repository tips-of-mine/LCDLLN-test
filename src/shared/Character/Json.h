#pragma once
// Mini-parseur JSON partagé (le projet n'expose pas de bibliothèque JSON ;
// repris du parser éprouvé de SlashCommandRegistry.cpp, généralisé en
// engine::json). Subset : object, array, string, number, bool, null.
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::json
{
    struct Value;
    using Object = std::unordered_map<std::string, Value>;
    using Array  = std::vector<Value>;

    struct Value
    {
        enum class Type { Null, Bool, Number, String, Array, Object };
        Type        type = Type::Null;
        bool        b    = false;
        double      n    = 0.0;
        std::string s;
        Array       a;
        Object      o;

        /// Retourne le membre \p key si \c this est un objet le contenant, sinon nullptr.
        const Value* Find(const std::string& key) const
        {
            if (type != Type::Object) return nullptr;
            auto it = o.find(key);
            return it == o.end() ? nullptr : &it->second;
        }
    };

    /// Parse un document JSON UTF-8 complet. Retourne true si tout le document
    /// est consommé sans erreur.
    bool Parse(const std::string& src, Value& out);
}
