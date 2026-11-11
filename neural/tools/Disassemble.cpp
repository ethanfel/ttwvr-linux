// SPDX-License-Identifier: GPL-3.0-only
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdio>
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;
 unsigned written=0;
 for(const auto& item:std::filesystem::directory_iterator(argv[1])){
  if(item.path().extension()!=L".bin")continue;
  std::ifstream in(item.path(),std::ios::binary);std::vector<char> data((std::istreambuf_iterator<char>(in)),{});
  Microsoft::WRL::ComPtr<ID3DBlob> assembly;
  auto hr=D3DDisassemble(data.data(),data.size(),0,nullptr,&assembly);
  if(FAILED(hr)){printf("disassemble_failed=%08lx\n",(unsigned long)hr);return 3;}
  auto path=item.path();path.replace_extension(L".asm");std::ofstream out(path,std::ios::binary);out.write((const char*)assembly->GetBufferPointer(),assembly->GetBufferSize());if(!out)return 4;++written;
 }
 printf("disassembled=%u\n",written);return written?0:5;
}
