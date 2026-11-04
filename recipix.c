/* Recipix - an original recipe viewer for Horizon (HMod, wasm component).
 * Written from scratch in C against the public horizon:mods@0.1.0 interfaces. */
typedef unsigned char u8; typedef unsigned int u32; typedef int i32; typedef unsigned long long u64;

#define IMP(m,n) __attribute__((import_module("horizon:mods/" m "@0.1.0"), import_name(n)))
#define EXP(n) __attribute__((export_name(n)))

/* ---- libc bits the compiler may call ---- */
void *memcpy(void *d, const void *s, unsigned long n){ u8*a=d; const u8*b=s; while(n--)*a++=*b++; return d; }
void *memmove(void *d, const void *s, unsigned long n){ u8*a=d; const u8*b=s; if(a<b){while(n--)*a++=*b++;} else {a+=n;b+=n;while(n--)*--a=*--b;} return d; }
void *memset(void *d, int c, unsigned long n){ u8*a=d; while(n--)*a++=(u8)c; return d; }
int memcmp(const void *x, const void *y, unsigned long n){ const u8*a=x,*b=y; while(n--){ if(*a!=*b) return *a-*b; a++;b++;} return 0; }

/* ---- bump allocator ---- */
extern u8 __heap_base;
static u32 heap_ptr, heap_start, persist_top;
static void *balloc(u32 size, u32 align){
  if(!heap_ptr){ heap_ptr = heap_start = (u32)&__heap_base; }
  heap_ptr = (heap_ptr + align - 1) & ~(align - 1);
  u32 p = heap_ptr; heap_ptr += size;
  u32 need = (heap_ptr + 65535) >> 16, have = __builtin_wasm_memory_size(0);
  if(need > have && __builtin_wasm_memory_grow(0, need - have) == (u32)-1) __builtin_trap();
  return (void*)p;
}
EXP("cabi_realloc") void *cabi_realloc(void *old, u32 osz, u32 align, u32 nsz){
  if(nsz == 0) return (void*)align;
  void *p = balloc(nsz, align);
  if(old && osz) memcpy(p, old, osz < nsz ? osz : nsz);
  return p;
}

/* ---- host layouts (canonical ABI, wasm32) ---- */
typedef struct { const u8 *p; u32 n; } WStr;
typedef struct { WStr id, name; WStr *tags; u32 ntags; } WItem;                 /* 24 */
typedef struct { u32 item, count; WStr comps; } RStack;                          /* 16 */
typedef struct { RStack *p; u32 n; } RList;
typedef struct { WStr id; u8 kind, pad[3]; WStr category, group; u32 width, height;
                 RList *slots; u32 nslots; RStack *res; u32 nres; u32 cooking; float xp; i32 book; } WRecipe; /* 64 */
typedef struct { WStr item; u32 count; WStr comps; } BStack;                     /* 20 */
typedef struct { BStack *p; u32 n; } BList;
typedef struct { u8 layout, pad[3]; u32 w, h; BList *slots; u32 nslots; BStack *res; u32 nres; WStr note;
                 u8 has_action, action, pad2[2]; } BView;                         /* 40 */

IMP("game","get-player") void h_get_player(void *ret);
IMP("recipes","items") void h_items(u32 *ret);
IMP("recipes","all") void h_all(u32 *ret);
IMP("browser","set-items") void h_set_items(BStack *p, u32 n);
IMP("browser","set-recipes") void h_set_recipes(const u8 *t, u32 tl, BStack *tabs, u32 nt, u32 tab, BView *v, u32 nv);
IMP("browser","poll") void h_bpoll(u32 *ret);
IMP("input","poll") void h_ipoll(u32 *ret);
IMP("input","register-key") void h_regkey(const u8*,u32,const u8*,u32,const u8*,u32,u32 *ret);

/* ---- state ---- */
static int loaded;
static WItem *items; static u32 nitems;
static WRecipe *recs; static u32 nrecs;
static u32 *listed; static u32 nlisted;
static char search[256]; static u32 searchlen;

#define MAX_RECIPES 256
#define MAX_STACKS 64
#define MAX_HIST 32
typedef struct { char id[128]; u32 idn; char comps[1024]; u32 compn; int uses; u32 tab; } View;
static View hist[MAX_HIST]; static u32 nhist;

static const char *STATION[8] = {"minecraft:crafting_table","minecraft:furnace","minecraft:blast_furnace",
  "minecraft:smoker","minecraft:campfire","minecraft:stonecutter","minecraft:smithing_table","minecraft:brewing_stand"};
static const u8 KIND_STATION[9] = {0,0,1,2,3,4,5,6,7};

static u32 slen(const char *s){ u32 n=0; while(s[n]) n++; return n; }
static int weq(WStr a, const char *b, u32 bn){ return a.n == bn && memcmp(a.p, b, bn) == 0; }
static u8 lc(u8 c){ return (c>='A'&&c<='Z') ? c+32 : c; }
static int ci_contains(WStr h, const char *nd, u32 nn){
  if(nn == 0) return 1; if(h.n < nn) return 0;
  for(u32 i=0;i+nn<=h.n;i++){ u32 j=0; while(j<nn && lc(h.p[i+j])==(u8)nd[j]) j++; if(j==nn) return 1; }
  return 0;
}

static BStack mk(WStr id, u32 count, WStr comps){ BStack b; b.item=id; b.count=count; b.comps=comps; return b; }
static BStack mk_c(const char *id){ WStr s={(const u8*)id, slen(id)}; WStr e={(const u8*)"",0}; return mk(s,1,e); }
static BStack convert(const RStack *s){
  static const char *air="minecraft:air";
  WStr id = s->item < nitems ? items[s->item].id : (WStr){(const u8*)air, 13};
  return mk(id, s->count, s->comps);
}
static const char *ITEM_NAME_FALLBACK = "";
static WStr name_of(const char *id, u32 n){
  for(u32 i=0;i<nitems;i++) if(weq(items[i].id,id,n)) return items[i].name;
  return (WStr){(const u8*)id, n}; (void)ITEM_NAME_FALLBACK;
}

static int stack_matches(const RStack *s, const View *v){
  if(s->item >= nitems) return 0;
  if(!weq(items[s->item].id, v->id, v->idn)) return 0;
  return v->compn == 0 || (s->comps.n == v->compn && memcmp(s->comps.p, v->comps, v->compn) == 0);
}
static int recipe_matches(const WRecipe *r, const View *v){
  if(v->uses){
    for(u32 i=0;i<r->nslots;i++) for(u32 j=0;j<r->slots[i].n;j++) if(stack_matches(&r->slots[i].p[j], v)) return 1;
    return 0;
  }
  for(u32 i=0;i<r->nres;i++) if(stack_matches(&r->res[i], v)) return 1;
  return 0;
}

/* ---- item list + search ---- */
static void list(void){
  const char *words[16]; u32 wl[16]; u32 nw=0;
  char low[256]; for(u32 i=0;i<searchlen;i++) low[i]=(char)lc((u8)search[i]);
  for(u32 i=0;i<searchlen && nw<16;){
    while(i<searchlen && low[i]<=' ') i++;
    u32 s=i; while(i<searchlen && low[i]>' ') i++;
    if(i>s){ words[nw]=low+s; wl[nw]=i-s; nw++; }
  }
  nlisted=0;
  for(u32 n=0;n<nitems;n++){
    WItem *it=&items[n];
    if(weq(it->id,"minecraft:air",13)) continue;
    int keep=1;
    for(u32 w=0;w<nw && keep;w++){
      const char *t=words[w]; u32 tn=wl[w];
      if(t[0]=='@'){
        u32 c=0; while(c<it->id.n && it->id.p[c]!=':') c++;
        WStr ns={it->id.p,c}; u32 pn=tn-1; keep = pn<=c && (pn==0 || ({ int ok=1; for(u32 k=0;k<pn;k++) if(lc(ns.p[k])!=(u8)t[1+k]){ok=0;break;} ok; }));
      } else if(t[0]=='#'){
        int any=0; for(u32 k=0;k<it->ntags;k++) if(ci_contains(it->tags[k],t+1,tn-1)){any=1;break;}
        keep=any;
      } else keep = ci_contains(it->name,t,tn) || ci_contains(it->id,t,tn);
    }
    if(keep) listed[nlisted++]=n;
  }
  BStack *st = balloc(nlisted*sizeof(BStack)+4, 4);
  for(u32 i=0;i<nlisted;i++) st[i]=mk(items[listed[i]].id,1,(WStr){(const u8*)"",0});
  h_set_items(st, nlisted);
}

/* ---- number formatting ---- */
static u32 put_u(char *b, u32 v){ char t[12]; u32 n=0; do{ t[n++]='0'+v%10; v/=10; }while(v); for(u32 i=0;i<n;i++) b[i]=t[n-1-i]; return n; }
static u32 put_f(char *b, float f){
  u32 x=(u32)(f*100.0f+0.5f), n=put_u(b,x/100), fr=x%100;
  if(fr){ b[n++]='.'; b[n++]='0'+fr/10; if(fr%10) b[n++]='0'+fr%10; }
  return n;
}
static u32 put_s(char *b, const char *s){ u32 n=slen(s); memcpy(b,s,n); return n; }

/* ---- recipe page ---- */
static BView make_view(const WRecipe *r){
  BView v; memset(&v,0,sizeof v);
  u32 ns=r->nslots;
  BList *slots = balloc((ns+9)*sizeof(BList),4);
  for(u32 i=0;i<ns;i++){
    u32 n = r->slots[i].n; if(n>MAX_STACKS) n=MAX_STACKS;
    BStack *b = balloc(n*sizeof(BStack)+4,4);
    for(u32 j=0;j<n;j++) b[j]=convert(&r->slots[i].p[j]);
    slots[i].p=b; slots[i].n=n;
  }
  u32 nr = r->nres; if(nr>MAX_STACKS) nr=MAX_STACKS;
  BStack *res = balloc(nr*sizeof(BStack)+4,4);
  for(u32 j=0;j<nr;j++) res[j]=convert(&r->res[j]);
  v.res=res; v.nres=nr;
  v.w=1; v.h=1;
  char *note = balloc(64,1); u32 nn=0;
  switch(r->kind){
    case 0: v.layout=0; v.w=r->width; v.h=r->height; break;
    case 1: { u32 w = ns<=1?1: ns<=4?2:3; u32 h=(ns+w-1)/w; if(h<1)h=1; u32 tot=w*h;
      for(u32 i=ns;i<tot;i++){ slots[i].p=0; slots[i].n=0; } ns=tot; v.layout=0; v.w=w; v.h=h;
      nn=put_s(note,"Shapeless"); break; }
    case 2: case 3: case 4: case 5:
      v.layout=1; nn=put_f(note,(float)r->cooking/20.0f); nn+=put_s(note+nn," s, "); nn+=put_f(note+nn,r->xp); nn+=put_s(note+nn," XP"); break;
    case 6: v.layout=2; break;
    case 7: v.layout=3; v.w=3; break;
    default: v.layout=4; v.w=2; break;
  }
  v.slots=slots; v.nslots=ns;
  v.note=(WStr){(const u8*)note,nn};
  if(r->book>=0){ v.has_action=0; }  /* no "+" action in Recipix */
  return v;
}

static void show(void){
  if(!nhist){ h_set_recipes((const u8*)"",0,0,0,0,0,0); return; }
  View *v=&hist[nhist-1];
  u32 *found = balloc((nrecs+1)*4,4), nf=0;
  u32 present[8]={0};
  for(u32 i=0;i<nrecs;i++) if(recipe_matches(&recs[i],v)){ found[nf++]=i; present[KIND_STATION[recs[i].kind<9?recs[i].kind:0]]=1; }
  u32 st[8], ns=0; for(u32 s=0;s<8;s++) if(present[s]) st[ns++]=s;
  if(v->tab>=ns) v->tab = ns?ns-1:0;
  u32 total=0, on=0;
  BView *views = balloc(MAX_RECIPES*sizeof(BView),4);
  /* views must be built before further allocs are reused; each make_view allocates its own */
  for(u32 i=0;i<nf;i++){
    const WRecipe *r=&recs[found[i]];
    if(ns && KIND_STATION[r->kind<9?r->kind:0]==st[v->tab]){ total++; if(on<MAX_RECIPES) views[on++]=make_view(r); }
  }
  char *title=balloc(256,1); u32 tn=0;
  tn+=put_s(title+tn, v->uses?"Uses: ":"Recipes: ");
  WStr nm=name_of(v->id,v->idn); if(nm.n>150) nm.n=150; memcpy(title+tn,nm.p,nm.n); tn+=nm.n;
  if(total>on){ tn+=put_s(title+tn," ("); tn+=put_u(title+tn,on); tn+=put_s(title+tn," of "); tn+=put_u(title+tn,total); title[tn++]=')'; }
  BStack *tabs=balloc(8*sizeof(BStack)+4,4);
  for(u32 i=0;i<ns;i++) tabs[i]=mk_c(STATION[st[i]]);
  h_set_recipes((const u8*)title,tn,tabs,ns,v->tab,views,on);
}

static void open_item(WStr id, WStr comps, int uses){
  if(id.n==0 || id.n>=sizeof hist[0].id || comps.n>sizeof hist[0].comps) return;
  View nv; memcpy(nv.id,id.p,id.n); nv.idn=id.n; memcpy(nv.comps,comps.p,comps.n); nv.compn=comps.n; nv.uses=uses; nv.tab=0;
  u32 any=0; for(u32 i=0;i<nrecs && !any;i++) any=recipe_matches(&recs[i],&nv);
  if(!any) return;
  if(nhist==MAX_HIST){ memmove(&hist[0],&hist[1],sizeof(View)*(MAX_HIST-1)); nhist--; }
  hist[nhist++]=nv; show();
}

static void load(void){
  u8 pl[96] __attribute__((aligned(8))); pl[0]=0; h_get_player(pl);
  int in_world = pl[0] != 0;
  if(in_world && !loaded){
    u32 r[2]; h_items(r); items=(WItem*)r[0]; nitems=r[1];
    h_all(r); recs=(WRecipe*)r[0]; nrecs=r[1];
    listed=balloc((nitems+1)*4,4); persist_top=heap_ptr; loaded=1; nhist=0; searchlen=0;
    list();
  } else if(!in_world && loaded){
    h_set_recipes((const u8*)"",0,0,0,0,0,0); h_set_items(0,0);
    loaded=0; nhist=0; nitems=nrecs=nlisted=0; items=0; recs=0;
  }
}

static void handle(const u8 *e){
  const u32 *w=(const u32*)e; u32 tag=e[0];
  switch(tag){
    case 1: { u32 n=w[2]; if(n>sizeof search) n=sizeof search; memcpy(search,(const void*)w[1],n); searchlen=n; list(); break; }
    case 0: { u32 idx=w[1]; int right=e[8]; if(idx<nlisted){ WItem*it=&items[listed[idx]]; open_item(it->id,(WStr){(const u8*)"",0},right); } break; }
    case 2: { WStr k={(const u8*)w[1],w[2]}; WStr id={(const u8*)w[3],w[4]}; WStr c={(const u8*)w[6],w[7]};
      if(weq(k,"recipe",6)) open_item(id,c,0); else if(weq(k,"uses",4)) open_item(id,c,1); break; }
    case 3: { WStr id={(const u8*)w[3],w[4]}; WStr c={(const u8*)w[6],w[7]}; open_item(id,c,e[32]); break; }
    case 7: if(nhist){ hist[nhist-1].tab=w[1]; show(); } break;
    case 6: if(nhist){ nhist--; show(); } break;
    default: break;
  }
}

/* ---- exports: horizon:mods/events ---- */
static u32 init_ret[4] __attribute__((aligned(8)));
EXP("horizon:mods/events@0.1.0#init") u32 *mod_init(void){
  u32 r[2];
  h_regkey((const u8*)"recipe",6,(const u8*)"Show recipes",12,(const u8*)"key.keyboard.r",14,r);
  h_regkey((const u8*)"uses",4,(const u8*)"Show uses",9,(const u8*)"key.keyboard.u",14,r);
  u8 *b=(u8*)init_ret; memset(b,0,16);
  b[12]=0; b[13]=1; b[14]=0;           /* ok(registration{commands:[], tick:false, frame:true, chat:false}) */
  return init_ret;
}
EXP("horizon:mods/events@0.1.0#tick") void mod_tick(u64 t){ (void)t; }
EXP("horizon:mods/events@0.1.0#frame") void mod_frame(float s){
  (void)s;
  heap_ptr = loaded ? persist_top : heap_start;
  load();
  u32 r[2];
  heap_ptr = loaded ? persist_top : heap_ptr;
  h_ipoll(r);
  h_bpoll(r);
  const u8 *ev=(const u8*)r[0]; u32 n=r[1];
  for(u32 i=0;i<n;i++) handle(ev+i*36);
}
static u32 cmd_ret[4] __attribute__((aligned(8)));
EXP("horizon:mods/events@0.1.0#command") u32 *mod_command(const u8*a,u32 b,const u8*c,u32 d){
  (void)a;(void)b;(void)c;(void)d; memset(cmd_ret,0,16); return cmd_ret;   /* ok(none) */
}
EXP("horizon:mods/events@0.1.0#chat") void mod_chat(const u8*a,u32 b){ (void)a;(void)b; }
