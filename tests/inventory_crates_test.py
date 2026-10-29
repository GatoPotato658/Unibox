from pathlib import Path
import argparse
import json
import os
import re
import subprocess
import tempfile

args = argparse.ArgumentParser(description="Audit crate resolution against a TF2 items_game.txt using the actual C++ resolver.")
args.add_argument("schema", type=Path)
schema_path = args.parse_args().schema
repo = Path(__file__).resolve().parents[1]
work = tempfile.TemporaryDirectory(prefix="unibox-crates-")
work_path = Path(work.name)
import re
from pathlib import Path
s=schema_path.read_text()
tokens=re.finditer(r'//[^\n]*|"((?:\\.|[^"\\])*)"|([{}])|([^\s{}"]+)',s)
t=[m.group(1) if m.group(1) is not None else m.group(2) or m.group(3) for m in tokens if not m.group().startswith('//')]
i=0
def parse():
 global i
 d={}
 while i<len(t):
  k=t[i];i+=1
  if k=='}': return d
  v=t[i];i+=1
  d[k]=parse() if v=='{' else v
 return d
schema=parse()['items_game']
def merged(d,depth=0):
 r={}
 if depth>12:return r
 for p in d.get('prefab','').split():
  r=merge(r,merged(schema['prefabs'].get(p,{}),depth+1))
 return merge(r,d)
def merge(a,b):
 r=dict(a) if isinstance(a,dict) else {}
 for k,v in b.items():r[k]=merge(r.get(k,{}),v) if isinstance(v,dict) else v
 return r

root={k:schema[k]for k in ['client_loot_lists','item_collections','revolving_loot_lists']};root['items']={i:merged(d)for i,d in schema['items'].items()if i.isdigit() and 0<int(i)<=65535}
def vdf(d):
 return '\n'.join(json.dumps(k,ensure_ascii=False)+' '+('{\n'+vdf(v)+'\n}' if isinstance(v,dict) else json.dumps(str(v),ensure_ascii=False)) for k,v in d.items())
(work_path/'schema.vdf').write_text(vdf(root))
s=(repo/'Unibox/src/Features/Visuals/SkinChanger/SkinChanger.cpp').read_text();a=s.index('\tstruct LocalCrateProfile_t');b=s.index('\n\tvoid PopulateCrateAttributes(',a);code=s[a:b];a=code.index('\tKeyValues* LoadCrateSchema()');b=code.index('\n\tusing CrateSeriesMap_t',a);code=code[:a]+'\tKeyValues* LoadCrateSchema() { return gSchema; }\n'+code[b:]
a=code.index('\tCrateSeriesMap_t LoadCrateCache(');b=code.index('\n\tvoid BuildCrateProfiles(',a);code=code[:a]+'\tCrateSeriesMap_t gDiskCache;\n\tCrateSeriesMap_t LoadCrateCache(const std::unordered_map<uint16_t, KeyValues*>&) { return gDiskCache; }\n\tvoid SaveCrateCache() {}\n'+code[b:]
pre=r'''#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cassert>
#include "CRATE_DATA_HEADER"
struct KeyValues {
 uint8_t pad[40]={};KeyValues* peer=nullptr;KeyValues* sub=nullptr;std::string name,value;
 KeyValues* FindKey(const char* path){if(!path||!*path)return this;std::string p=path;auto i=p.find('/');auto first=p.substr(0,i);for(auto* c=sub;c;c=c->peer)if(c->name==first)return i==p.npos?c:c->FindKey(p.c_str()+i+1);return nullptr;}
 const char* GetName(){return name.c_str();}
 const char* GetString(const char* path,const char* def=""){auto* k=FindKey(path);return k?k->value.c_str():def;}
 int GetInt(const char* path,int def=0){auto* k=FindKey(path);return k&&!k->value.empty()?atoi(k->value.c_str()):def;}
 float GetFloat(const char* path,float def=0){auto* k=FindKey(path);return k&&!k->value.empty()?strtof(k->value.c_str(),nullptr):def;}
};
template<class T>T& Field(void* p,size_t n){return *reinterpret_cast<T*>(static_cast<uint8_t*>(p)+n);}
struct KeyValuesAD{KeyValues* p;KeyValuesAD(KeyValues* k):p(k){}operator KeyValues*(){return p;}KeyValues* operator->(){return p;}};
KeyValues* gSchema=nullptr;
KeyValues* parse(std::istream& in){auto* root=new KeyValues;KeyValues** tail=&root->sub;for(;;){in>>std::ws;if(in.peek()==EOF)break;if(in.peek()=='}'){in.get();break;}std::string key,val;in>>std::quoted(key);in>>std::ws;KeyValues* child;if(in.peek()=='{'){in.get();child=parse(in);}else{in>>std::quoted(val);child=new KeyValues;child->value=val;}child->name=key;*tail=child;tail=&child->peer;}return root;}
'''
post=r'''
int main(int argc,char** argv){assert(argc==2);std::ifstream in(argv[1]);gSchema=parse(in);std::unordered_map<std::string,uint16_t> names;std::unordered_map<uint16_t,KeyValues*> defs;for(auto* d=CrateFirstKey(gSchema->FindKey("items"));d;d=d->peer){uint16_t id=atoi(d->GetName());names.emplace(d->GetString("name"),id);defs.emplace(id,d);}BuildCrateProfiles(names,defs);int ok=0,failed=0;for(auto&[id,p]:s_mCrateProfiles){if(p.vItems.empty()){std::cout<<"MISSING "<<id<<" "<<defs[id]->GetString("name")<<" series "<<p.iSeries<<"\n";failed++;}else{ok++;for(auto v:p.vItems)assert(defs.contains(v));}}assert(s_mCrateProfiles[5048].iKeyDef==5049);assert(s_mCrateProfiles[5048].iAltKeyDef==5021);assert(s_mCrateProfiles[5642].iKeyDef==5643);std::cout<<"Crate definitions covered: "<<ok<<"; missing: "<<failed<<"\n";assert(s_mCrateProfiles[5807].vItems.size()==15);assert(s_mCrateProfiles[15000].vItems.empty());assert(s_mCrateProfiles[5737].vItems.size()==44);assert(s_mCrateProfiles[5642].vItems.size()==8);assert(!s_mCrateProfiles[18000].iKeyDef);assert(!failed);
 {std::istringstream valid("UNIBOX_CRATE_LOOT_CACHE 1\n62 3 30090 30090 65534\n");auto m=ReadCrateCache(valid,defs);assert(m[62]==std::vector<uint16_t>{30090});}
 for(auto data:{"bad 1", "UNIBOX_CRATE_LOOT_CACHE 2", "UNIBOX_CRATE_LOOT_CACHE 1\n62 2 30090", "UNIBOX_CRATE_LOOT_CACHE 1\n62", "UNIBOX_CRATE_LOOT_CACHE 1\n62 1000000"}){std::istringstream input(data);assert(ReadCrateCache(input,defs).empty());}
 gDiskCache=s_mCrateSeries;auto* removed=gSchema->FindKey("client_loot_lists/supply_crate_loot_list_1");assert(removed);removed->name="removed_in_future_update";BuildCrateProfiles(names,defs);assert(s_mCrateProfiles[5022].vItems==gDiskCache[1]);
 std::cout<<"PASS cache validation and removed-list recovery\n";

 KeyValues root,lists,outer,inner,collectionRef,collectionRoot,collection,items,rarity,item,loop,loopRef,meta;
 root.sub=&lists;lists.name="client_loot_lists";lists.peer=&collectionRoot;collectionRoot.name="item_collections";collectionRoot.sub=&collection;
 collection.name="test_collection";collection.sub=&items;items.name="items";items.sub=&rarity;rarity.name="rare";rarity.sub=&item;item.name="test_item";item.value="1";
 lists.sub=&outer;outer.name="outer";outer.sub=&inner;inner.name="inner";inner.value="1";outer.peer=&loop;loop.name="loop";loop.sub=&loopRef;loopRef.name="loop";loopRef.value="1";
 loop.peer=&meta;meta.name="inner";meta.sub=&collectionRef;collectionRef.name="loot_list_collection";collectionRef.value="test_collection";
 std::unordered_map<std::string,uint16_t> toyNames={{"test_item",42}};CrateSchemaReader reader(&root,toyNames);
 auto result=reader.Read("outer");assert(result==std::vector<uint16_t>{42});assert(reader.Read("loop").empty());assert(reader.Read("unknown").empty());inner.value="0";assert(reader.Read("outer").empty());
 std::cout<<"PASS nested collections, zero weights, unknown lists and cycle protection\n";
}
'''
pre = pre.replace("CRATE_DATA_HEADER", (repo/"Unibox/src/Features/Visuals/SkinChanger/CrateLootData.h").as_posix())
source_path = work_path/"test.cpp"
source_path.write_text(pre+code+post)
binary = work_path/"test"
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O2", str(source_path), "-o", str(binary)], check=True)
subprocess.run([str(binary), str(work_path/"schema.vdf")], check=True)
work.cleanup()
