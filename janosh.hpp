#ifndef _JANOSH_HPP
#define _JANOSH_HPP

#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <functional>
#include <algorithm>
#include <exception>
#include <initializer_list>

#include <boost/bind.hpp>
#include <boost/thread.hpp>
#include <boost/range.hpp>
#include <boost/tokenizer.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/functional/hash.hpp>
#include <boost/function.hpp>
#include <boost/interprocess/creation_tags.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <kcpolydb.h>

#include "logger.hpp"
#include "record.hpp"
#include "json_spirit/json_spirit.h"
#include "json.hpp"
#include "bash.hpp"

  namespace kc = kyotocabinet;
  namespace js = json_spirit;
  namespace fs = boost::filesystem;
  using std::cerr;
  using std::cout;
  using std::endl;
  using std::string;
  using std::vector;
  using std::map;
  using std::istringstream;
  using std::ifstream;
  using std::exception;
namespace janosh {

  enum Format {
    Bash,
    Json,
    Raw
  };

  class TriggerBase {
    typedef std::map<string,string> TargetMap;
    typedef typename TargetMap::value_type Target;

    vector<fs::path> targetDirs;
    map<Path, std::set<string> > triggers;
    TargetMap targets;
public:
    TriggerBase(const fs::path& config, const vector<fs::path>& targetDirs);

    int executeTarget(const string& name);

    void executeTrigger(const Path& p);
    bool findAbsoluteCommand(const string& cmd, string& abs);
    void load(const fs::path& config);
    void load(std::ifstream& is);
    template<typename T> void error(const string& msg, T t, int exitcode=1) {
      LOG_ERR_MSG(msg, t);
      exit(exitcode);
    }

    template<typename T> void warn(const string& msg, T t) {
      LOG_WARN_MSG(msg, t);
    }
  };

  class Settings {
  public:
    fs::path janoshFile;
    fs::path databaseFile;
    fs::path triggerFile;
    fs::path logFile;
    vector<fs::path> triggerDirs;

    Settings();
    template<typename T> void error(const string& msg, T t, int exitcode=1) {
      LOG_ERR_MSG(msg, t);
      exit(exitcode);
    }
  private:
    bool find(const js::Object& obj, const string& name, js::Value& value);
  };

  class Command;
  typedef map<const std::string, Command*> CommandMap;

  class Janosh {
  public:
    typedef boost::function<void(int)> ExitHandler;
    Settings settings_;
    TriggerBase triggers_;
    CommandMap cm;

      Janosh();
    ~Janosh();

    void setFormat(Format f) ;
    void printException(janosh_exception& ex);
    void printException(std::exception& ex);
    void open(bool readOnly);
    void close();
    size_t process(int argc, char** argv);

    size_t loadJson(const string& jsonfile);
    size_t loadJson(std::istream& is);

    size_t makeArray(Record target, size_t size = 0, bool boundsCheck=true);
    size_t makeObject(Record target, size_t size = 0);
    size_t makeDirectory(Record target, Value::Type type, size_t size = 0);
    size_t get(Record target, std::ostream& out);
    size_t size(Record target);
    size_t remove(Record& target, bool pack=true);

    size_t add(Record target, const string& value);
    size_t replace(Record target, const string& value);
    size_t set(Record target, const string& value);
    size_t append(Record target, const string& value);
    size_t append(vector<string>::const_iterator begin, vector<string>::const_iterator end, Record dest);

    size_t move(Record& src, Record& dest);
    size_t replace(Record& src, Record& dest);
    size_t append(Record& src, Record& dest);
    size_t copy(Record& src, Record& dest);
    size_t shift(Record& src, Record& dest);

    size_t dump();
    size_t hash();
    size_t truncate();
  private:
    Format format;

    bool open_;

    Format getFormat();

    bool isOpen();

    void terminate(int code);

    string filename;
    js::Value rootValue;

    void setContainerSize(Record rec, const size_t s);
    void changeContainerSize(Record rec, const size_t by);

    size_t load(const Path& path, const string& value);
    size_t load(js::Value& v, Path& path);
    size_t load(js::Object& obj, Path& path);
    size_t load(js::Array& array, Path& path);
    bool boundsCheck(Record p);
    Record makeTemp(const Value::Type& t);

    template<typename Tvisitor>
     size_t recurse(Record& travRoot, Tvisitor vis)  {
       size_t cnt = 0;
       std::stack<std::pair<const Component, const Value::Type> > hierachy;
       Record root("/.");

       Record rec(travRoot);
       vis.begin();

       Path last;
       do {
         rec.fetch();
         const Path& path = rec.path();
         const Value& value = rec.value();
         const Value::Type& t = rec.getType();
         const Path& parent = path.parent();

         const Component& name = path.name();
         const Component& parentName = parent.name();

         if (!hierachy.empty()) {
           if (!travRoot.isAncestorOf(path)) {
             break;
           }

           if(!last.above(path) && (
               (!last.isDirectory() && parentName != last.parentName()) ||
               (last.isDirectory() && parentName != last.name()))){
             while(!hierachy.empty() && hierachy.top().first != parentName) {
               if (hierachy.top().second == Value::Array) {
                 vis.endArray(path);
               } else if (hierachy.top().second == Value::Object) {
                 vis.endObject(path);
               }
               hierachy.pop();
             }
           }
         }

         if (t == Value::Array) {
           hierachy.push({name, Value::Array});
           vis.beginArray(path, last.isEmpty() || last == parent);
         } else if (t == Value::Object) {
           hierachy.push({name, Value::Object});
           vis.beginObject(path, last.isEmpty() || last == parent);
         } else {
           bool first = last.isEmpty() || last == parent;
           if(!hierachy.empty()){
             vis.record(path, value, hierachy.top().second == Value::Array, first);
           } else {
             vis.record(path, value, false, first);
           }
         }
         last = path;
         ++cnt;
       } while (rec.step());

       while (!hierachy.empty()) {
           if (hierachy.top().second == Value::Array) {
             vis.endArray("");
           } else if (hierachy.top().second == Value::Object) {
             vis.endObject("");
           }
           hierachy.pop();
       }

       vis.close();
       return cnt;
     }
  };

  class RawPrintVisitor {
    std::function<void(const string&)> f;
    std::ostream& out;

  public:
    RawPrintVisitor(std::ostream& out) :
        out(out) {
    }

    void beginArray(const Path& p, bool first) {
    }

    void endArray(const Path& p) {
    }

    void beginObject(const Path& p, bool first) {
    }

    void endObject(const Path& p) {
    }

    void begin() {
    }

    void close() {
    }

    void record(const Path& p, const string& value, bool array, bool first) {
      string stripped = value;
      replace(stripped.begin(), stripped.end(), '\n', ' ');
      out << stripped << endl;
    }
  };
}
#endif
