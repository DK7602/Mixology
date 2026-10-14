// Writes plugin/ui/procspec.js from the processing control table (dsp/src/ProcParams.cpp), or checks that the
// checked-in copy is current. The interface reads ranges and defaults from it, so the pages and the plug-ins
// never disagree.
//
//   procspec <path to procspec.js>          check (exit 1 when it is out of date)
//   procspec <path to procspec.js> --write  regenerate it

#include "mixology/ProcParams.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
    std::string num (float v)
    {
        char buf[32];
        std::snprintf (buf, sizeof buf, "%.6g", static_cast<double> (v));
        return buf;
    }

    std::string generate()
    {
        using namespace mixology::proc;
        std::ostringstream js;
        js << "// Generated from dsp/src/ProcParams.cpp by tools/procspec. Do not edit: a test checks it is current.\n"
              "// Every processing control of Mixology Link (and of Mixology on the full mix), in registry order.\n\n"
              "export const PROC = [\n";
        for (const auto& s : specs())
        {
            js << "  { id: \"" << s.id << "\", name: \"" << s.name << "\", kind: \""
               << (s.kind == Kind::number ? "number" : s.kind == Kind::toggle ? "toggle" : "choice") << "\", min: " << num (s.min)
               << ", max: " << num (s.max) << ", def: " << num (s.def) << ", curve: \"" << (s.curve == Curve::log ? "log" : "lin")
               << "\", step: " << num (s.step) << ", unit: \"" << s.unit << "\"";
            if (s.kind != Kind::number)
            {
                js << ", choices: [";
                std::string c = s.choices, part;
                std::istringstream in (c);
                bool first = true;
                while (std::getline (in, part, '|'))
                {
                    js << (first ? "" : ", ") << "\"" << part << "\"";
                    first = false;
                }
                js << "]";
            }
            js << " },\n";
        }
        js << "];\n\nexport const PROC_INDEX = Object.fromEntries(PROC.map((p, i) => [p.id, i]));\n\n"
              "// Modules in the order the audio runs through them; bit i of the meters' \"running\" is MODULES[i].\n"
              "export const MODULES = [";
        for (int m = 0; m < kModules; ++m) js << (m ? ", " : "") << "\"" << moduleKey (static_cast<Module> (m)) << "\"";
        js << "];\nexport const MODULE_ON = {";
        for (int m = 0; m < kModules; ++m)
            js << (m ? ", " : " ") << moduleKey (static_cast<Module> (m)) << ": \"" << specs()[static_cast<std::size_t> (onParam (static_cast<Module> (m)))].id << "\"";
        js << " };\n";
        return js.str();
    }

    std::string withoutCarriageReturns (std::string s)
    {
        std::string out;
        for (char c : s)
            if (c != '\r') out += c;
        return out;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: procspec <procspec.js> [--write]\n");
        return 2;
    }
    const auto js = generate();
    if (argc > 2 && std::string (argv[2]) == "--write")
    {
        std::ofstream (argv[1], std::ios::binary) << js;
        std::printf ("wrote %s\n", argv[1]);
        return 0;
    }
    std::ifstream in (argv[1], std::ios::binary);
    std::stringstream current;
    current << in.rdbuf();
    if (withoutCarriageReturns (current.str()) != js)
    {
        std::printf ("%s is out of date: run  procspec %s --write  (built with the tests)\n", argv[1], argv[1]);
        return 1;
    }
    std::printf ("procspec.js matches the processing control table\n");
    return 0;
}
