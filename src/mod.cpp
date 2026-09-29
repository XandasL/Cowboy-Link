#include <mods/service.hpp>
#include <mods/svc/log.h>
#include <mods/svc/config.h>
#include <mods/svc/resource.h>
#include <mods/svc/overlay.h>
#include <mods/svc/texture.h>
#include <mods/svc/ui.h>
#include <dolphin/dvd.h>
#include "codec.hpp"
#include "generated.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(ResourceService, svc_resource);
IMPORT_SERVICE(OverlayService, svc_overlay);
IMPORT_SERVICE(TextureService, svc_texture);
IMPORT_SERVICE(UiService, svc_ui);

namespace {
using cowboy::Bytes;
using Clock=std::chrono::steady_clock;
ConfigVarHandle quality,style,hero,ordon,brightness,tint,custom;
OverlayHandle heroOverlay=0,ordonOverlay=0;
TextureReplacementHandle currentTexture=0;
UiElementHandle statusText=0,spectrum=0;
Bytes model,job;
size_t jobOffset=0;
uint32_t jobSize=0;
std::array<uint16_t,65536> lut;
std::string status="Preparing texture...",jobLabel;
bool pending=true,outfitsDirty=true;
auto due=Clock::now(),nextRead=Clock::now();

void check(ModResult r,const char* operation){if(r!=MOD_OK)throw std::runtime_error(std::string(operation)+" failed ("+std::to_string(r)+")");}
ModResult failed(ModError* e,const std::exception& ex){return mods::set_error(e,MOD_ERROR,ex.what());}
int64_t integer(ConfigVarHandle h){int64_t v=0;check(svc_config->get_int(mod_ctx,h,&v),"Read setting");return v;}
bool boolean(ConfigVarHandle h){bool v=false;check(svc_config->get_bool(mod_ctx,h,&v),"Read setting");return v;}
std::string hex(){
 size_t n=0;check(svc_config->get_string(mod_ctx,tint,nullptr,0,&n),"Read color length");
 if(n>32)return "FFFFFF";
 std::string s(n+1,'\0');check(svc_config->get_string(mod_ctx,tint,s.data(),s.size(),nullptr),"Read color");s.resize(n);
 if(!s.empty()&&s[0]=='#')s.erase(0,1);
 if(s.size()!=6)return "FFFFFF";
 for(char& c:s){if(!std::isxdigit(static_cast<unsigned char>(c)))return "FFFFFF";c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));}return s;
}
ConfigVarHandle variable(const char* name,ConfigVarType type,int64_t value=0,const char* text=nullptr){
 ConfigVarDesc d=CONFIG_VAR_DESC_INIT;d.name=name;d.type=type;d.default_bool=value!=0;d.default_int=value;d.default_string=text;
 ConfigVarHandle h=0;check(svc_config->register_var(mod_ctx,&d,&h),"Register setting");return h;
}
Bytes load(const std::string& path){
 ResourceBuffer b=RESOURCE_BUFFER_INIT;check(svc_resource->load(mod_ctx,path.c_str(),&b),"Load bundled resource");
 try{auto p=static_cast<uint8_t*>(b.data);Bytes result;if(b.size)result.assign(p,p+b.size);svc_resource->free(mod_ctx,&b);return result;}
 catch(...){svc_resource->free(mod_ctx,&b);throw;}
}
bool disc(const char* path,Bytes& out){
 DVDFileInfo f{};if(!DVDOpen(path,&f))return false;
 try{
  cowboy::need(f.length>0&&f.length<=32*1024*1024,"Unexpected game resource size");
  size_t size=f.length,padded=(size+31)&~size_t(31);Bytes storage(padded+31);
  auto p=reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(storage.data())+31)&~uintptr_t(31));
  auto read=DVDReadPrio(&f,p,static_cast<int32_t>(padded),0,2);
  cowboy::need(read>=static_cast<int32_t>(size),"Could not read game archive");
  out.assign(p,p+size);DVDClose(&f);return true;
 }catch(...){DVDClose(&f);throw;}
}
void reconstruct(){
 if(!model.empty()||Clock::now()<nextRead)return;
 nextRead=Clock::now()+std::chrono::seconds(1);
 Bytes a,b;if(!disc("/res/Object/Kmdl.arc",a)||!disc("/res/Object/Bmdl.arc",b))return;
 auto base=cowboy::resource(a,"al_head.bmd"),second=cowboy::resource(b,"bl_head.bmd");
 base.insert(base.end(),second.begin(),second.end());model=cowboy::apply(base,load("model.patch"));
 outfitsDirty=true;svc_log->info(mod_ctx,"Reconstructed and verified model from game archives; stable geometry and hair weights preserved.");
}
void overlays(){
 if(model.empty()||!outfitsDirty)return;
 auto update=[](bool enabled,OverlayHandle& handle,const char* path){
  if(enabled&&!handle)check(svc_overlay->add_buffer(mod_ctx,path,model.data(),model.size(),&handle),"Register model overlay");
  else if(!enabled&&handle){check(svc_overlay->remove(mod_ctx,handle),"Remove model overlay");handle=0;}
 };
 update(boolean(hero),heroOverlay,"/res/Object/Kmdl/archive/bmwr/al_head.bmd");
 update(boolean(ordon),ordonOverlay,"/res/Object/Bmdl/archive/bmwr/bl_head.bmd");outfitsDirty=false;
}
void publish(TextureReplacementHandle h,const std::string& label){
 auto previous=currentTexture;currentTexture=h;
 if(previous)check(svc_texture->unregister(mod_ctx,previous),"Remove old texture");
 status="Loaded: "+label;svc_log->info(mod_ctx,status.c_str());
}
std::array<int,3> color(){auto s=boolean(custom)?hex():"FFFFFF";auto v=std::stoul(s,nullptr,16);return {int(v>>16),int((v>>8)&255),int(v&255)};}
void startTexture(){
 const char* folders[]={"512","1k","2k","4k","vanilla"};const uint32_t sizes[]={512,1024,2048,4096,128};
 auto q=integer(quality);if(q<0||q>4)q=4;bool dark=integer(style)==1;
 std::string material=dark?"dark":"original",folder=folders[q];jobSize=sizes[q];
 jobLabel=std::string(dark?"Dark leather (Link)":"Original leather")+" / "+std::to_string(jobSize)+"x"+std::to_string(jobSize);
 auto c=color();double light=std::clamp<int64_t>(integer(brightness),25,200)/100.0;
 if(light==1&&c[0]==255&&c[1]==255&&c[2]==255){
  TextureReplacementHandle h=0;auto path="res/styles/"+material+"/"+folder+"/"+HatFilename;
  check(svc_texture->register_file(mod_ctx,path.c_str(),&h),"Register texture");publish(h,jobLabel);return;
 }
 job=load("colors/"+material+"/"+folder+".bc1");
 cowboy::need(job.size()==size_t(jobSize)*jobSize/2,"Unexpected BC1 texture size");jobOffset=0;
 for(unsigned i=0;i<65536;i++){
  auto channel=[&](unsigned v,int component,int max){return std::clamp(int(std::floor(v*c[component]/255.0*light+0.5)),0,max);};
  lut[i]=uint16_t(channel(i>>11,0,31)*2048+channel((i>>5)&63,1,63)*32+channel(i&31,2,31));
 }
 status="Adjusting "+jobLabel+"...";
}
void advance(){
 if(job.empty())return;size_t last=std::min(jobOffset+8192*8,job.size());
 for(size_t p=jobOffset;p<last;p+=8){
  uint16_t o0=job[p]|job[p+1]<<8,o1=job[p+2]|job[p+3]<<8,c0=lut[o0],c1=lut[o1];
  uint32_t indices=uint32_t(job[p+4])|uint32_t(job[p+5])<<8|uint32_t(job[p+6])<<16|uint32_t(job[p+7])<<24;
  if(o0>o1){if(c0<c1){std::swap(c0,c1);indices^=0x55555555;}else if(c0==c1){if(c0<65535)++c0;else --c1;}}
  else if(c0>c1){std::swap(c0,c1);indices^=(~(indices>>1))&0x55555555;}
  job[p]=uint8_t(c0);job[p+1]=uint8_t(c0>>8);job[p+2]=uint8_t(c1);job[p+3]=uint8_t(c1>>8);
  for(int i=0;i<4;i++)job[p+4+i]=uint8_t(indices>>(8*i));
 }
 jobOffset=last;if(last==job.size()){
  TextureKey k=TEXTURE_KEY_INIT;k.kind=TEXTURE_KEY_SOURCE;k.texture_hash=HatHash;k.width=k.height=128;k.gx_format=14;
  TextureData d=TEXTURE_DATA_INIT;d.data=job.data();d.size=job.size();d.width=d.height=jobSize;d.gx_format=78;
  TextureReplacementHandle h=0;check(svc_texture->register_data(mod_ctx,&k,&d,&h),"Register adjusted texture");publish(h,jobLabel+" - adjusted color");job.clear();
 }
}
void changed(ModContext*,ConfigVarHandle h,const ConfigVarValue*,const ConfigVarValue*,void*){
 if(h==hero||h==ordon){outfitsDirty=true;return;}
 pending=true;due=Clock::now()+std::chrono::milliseconds(200);job.clear();
}
void reset(ModContext*,void*){
 // Report service errors via the next update, without throwing across the callback ABI.
 svc_config->set_int(mod_ctx,brightness,100);svc_config->set_string(mod_ctx,tint,"FFFFFF");svc_config->set_bool(mod_ctx,custom,false);
}
ModResult panelUpdate(ModContext*,void*,ModError* e){try{
 auto text=status+(model.empty()?" | Waiting for original game archives":"");svc_ui->elem_set_text(mod_ctx,statusText,text.c_str());
 auto c=color();std::string rml;char row[180];
 for(double f:{.15,.3,.5,.75,1.0}){std::snprintf(row,sizeof(row),"<span style=\"display:inline-block;width:38px;height:18px;background-color:#%02x%02x%02x;\"> </span>",int(c[0]*f),int(c[1]*f),int(c[2]*f));rml+=row;}
 svc_ui->elem_set_rml(mod_ctx,spectrum,rml.c_str());return MOD_OK;
 }catch(const std::exception& ex){return failed(e,ex);}}
ModResult buildPanel(ModContext*,UiElementHandle pane,void*,ModError* e){try{
 auto text=[&](const char* s){check(svc_ui->pane_add_text(mod_ctx,pane,s,nullptr),"Add text");};
 auto add=[&](UiControlKind kind,const char* label,ConfigVarHandle h,const char*const* options=nullptr,size_t count=0){
  UiControlDesc d=UI_CONTROL_DESC_INIT;d.kind=kind;d.label=label;d.binding=UI_BINDING_CONFIG_VAR;d.config_var=h;d.options=options;d.option_count=count;
  if(kind==UI_CONTROL_NUMBER){d.min=25;d.max=200;d.step=5;d.suffix="%";}
  const char* presets[]={"FFFFFF","C9A77B","AD8060","809E72","7296BA","B77878"};
  if(kind==UI_CONTROL_COLOR){d.color_presets=presets;d.color_preset_count=6;}
  check(svc_ui->pane_add_control(mod_ctx,pane,&d,nullptr),label);
 };
 check(svc_ui->pane_add_section(mod_ctx,pane,"Hat by outfit"),"Add section");
 add(UI_CONTROL_TOGGLE,"Hero's Tunic",hero);add(UI_CONTROL_TOGGLE,"Ordon Outfit",ordon);
 text("After enabling or disabling, change area; if the model does not update, restart the game. Turning it off restores the original head for that outfit.");
 check(svc_ui->pane_add_section(mod_ctx,pane,"Texture and color"),"Add section");
 const char* styles[]={"Original leather","Dark leather (Link)"};
 const char* resolutions[]={"512x512","1K (1024x1024)","2K (2048x2048)","4K (4096x4096)","Vanilla (128x128)"};
 add(UI_CONTROL_DROPDOWN,"Leather style",style,styles,2);add(UI_CONTROL_DROPDOWN,"Resolution",quality,resolutions,5);
 check(svc_ui->pane_add_text(mod_ctx,pane,status.c_str(),&statusText),"Add status");
 add(UI_CONTROL_NUMBER,"Brightness",brightness);add(UI_CONTROL_TOGGLE,"Customize color",custom);add(UI_CONTROL_COLOR,"Color - white keeps the original",tint);
 check(svc_ui->pane_add_rml(mod_ctx,pane,"",&spectrum),"Add color preview");
 text("Color and brightness affect the hat on both outfits. In 4K, wait for the adjustment to finish.");
 UiControlDesc button=UI_CONTROL_DESC_INIT;button.label="Restore original color";button.on_pressed=reset;
 check(svc_ui->pane_add_control(mod_ctx,pane,&button,nullptr),"Add restore button");return panelUpdate(mod_ctx,nullptr,e);
 }catch(const std::exception& ex){return failed(e,ex);}}
}

MOD_EXPORT ModResult mod_initialize(ModError* e){try{
 quality=variable("texture_quality",CONFIG_VAR_INT,4);style=variable("texture_style",CONFIG_VAR_INT,-1);
 hero=variable("hero_hat",CONFIG_VAR_BOOL,1);ordon=variable("ordon_hat",CONFIG_VAR_BOOL,1);
 brightness=variable("brightness",CONFIG_VAR_INT,100);tint=variable("tint",CONFIG_VAR_STRING,0,"FFFFFF");custom=variable("custom_color",CONFIG_VAR_BOOL,0);
 if(integer(style)!=0&&integer(style)!=1)check(svc_config->set_int(mod_ctx,style,integer(quality)==4?1:0),"Migrate style");
 check(svc_config->set_string(mod_ctx,tint,hex().c_str()),"Normalize color");
 for(auto h:{quality,style,hero,ordon,brightness,tint,custom})check(svc_config->subscribe(mod_ctx,h,changed,nullptr,nullptr),"Subscribe setting");
 reconstruct();overlays();startTexture();pending=false;
 UiModsPanelDesc panel=UI_MODS_PANEL_DESC_INIT;panel.build=buildPanel;panel.update=panelUpdate;
 check(svc_ui->register_mods_panel(mod_ctx,&panel),"Register panel");return MOD_OK;
 }catch(const std::exception& ex){return failed(e,ex);}}
MOD_EXPORT ModResult mod_update(ModError* e){try{
 reconstruct();overlays();if(pending&&Clock::now()>=due){pending=false;startTexture();}advance();return MOD_OK;
 }catch(const std::exception& ex){return failed(e,ex);}}
MOD_EXPORT ModResult mod_shutdown(ModError*){
 job.clear();model.clear();
 if(currentTexture)svc_texture->unregister(mod_ctx,currentTexture);
 if(heroOverlay)svc_overlay->remove(mod_ctx,heroOverlay);
 if(ordonOverlay)svc_overlay->remove(mod_ctx,ordonOverlay);
 currentTexture=heroOverlay=ordonOverlay=0;return MOD_OK;
}