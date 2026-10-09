#include "potion_model.h"
#include "potion_hash.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define ANY 255
#define RULE(min_h,min_m,min_l,min_d,min_f,min_c,min_t,min_r, max_h,max_m,max_l,max_d,max_f,max_c,max_t,max_r, forbid) \
    .min_value={min_h,min_m,min_l,min_d,min_f,min_c,min_t,min_r}, \
    .max_value={max_h,max_m,max_l,max_d,max_f,max_c,max_t,max_r}, .forbidden_mask=(forbid)

static const potion_material_def_t MATERIALS[POTION_MATERIAL_COUNT] = {
    {0,"晨露","清晨凝结的灵性水滴",POTION_RARITY_COMMON,{1,0,1,0,0,1,0,0},900},
    {1,"苔藓","带着潮湿草木气息",POTION_RARITY_COMMON,{2,0,0,0,0,1,0,0},900},
    {2,"松脂","温热而黏稠的树脂",POTION_RARITY_COMMON,{2,0,0,0,1,0,0,0},900},
    {3,"铜砂","细碎的赤铜矿砂",POTION_RARITY_COMMON,{0,2,0,0,1,0,0,0},900},
    {4,"雾叶","在冷雾中舒展的叶片",POTION_RARITY_RARE,{2,0,1,0,0,2,0,0},7200},
    {5,"月盐","闪着微光的冷盐晶",POTION_RARITY_RARE,{0,2,2,0,0,2,0,0},7200},
    {6,"萤石","封存柔和亮光的矿石",POTION_RARITY_RARE,{0,2,3,0,0,0,0,0},7200},
    {7,"古木","纹路深沉的古老木片",POTION_RARITY_RARE,{3,1,0,1,0,0,0,0},7200},
    {8,"夜晶","吸收周围光线的晶体",POTION_RARITY_EPIC,{0,2,0,3,0,1,0,0},43200},
    {9,"雷种","内部传来细小雷鸣",POTION_RARITY_EPIC,{1,0,1,0,0,0,3,0},43200},
    {10,"星尘","从夜空落下的微光尘埃",POTION_RARITY_LEGENDARY,{0,1,3,2,0,1,1,0},172800},
    {11,"龙鳞","仍残留灼热气息的鳞片",POTION_RARITY_LEGENDARY,{0,2,0,0,3,0,1,3},172800},
};

static const potion_def_t POTIONS[POTION_TYPE_COUNT] = {
    {0,"微光药剂","散发安静微光",POTION_RARITY_COMMON,10,RULE(0,0,2,0,0,0,0,0,ANY,ANY,ANY,0,ANY,ANY,ANY,ANY,1u<<POTION_ATTR_DARK)},
    {1,"清醒药剂","带来草木般的清冽",POTION_RARITY_COMMON,20,RULE(2,0,0,0,0,1,0,0,ANY,ANY,ANY,ANY,1,ANY,ANY,ANY,0)},
    {2,"守护药剂","凝成坚固的护盾",POTION_RARITY_RARE,30,RULE(1,3,0,0,0,0,0,0,ANY,ANY,ANY,ANY,ANY,ANY,ANY,ANY,0)},
    {3,"幸运药剂","电光带来偶然好运",POTION_RARITY_RARE,40,RULE(0,0,1,0,0,0,2,0,ANY,ANY,ANY,1,ANY,ANY,ANY,ANY,0)},
    {4,"星夜药剂","同时容纳星光与夜色",POTION_RARITY_EPIC,50,RULE(0,0,2,2,0,0,0,0,ANY,ANY,ANY,ANY,0,ANY,ANY,ANY,1u<<POTION_ATTR_FIRE)},
    {5,"龙息药剂","迸发炽热的龙焰",POTION_RARITY_LEGENDARY,100,RULE(0,0,0,0,2,0,0,3,ANY,ANY,ANY,ANY,ANY,0,ANY,ANY,1u<<POTION_ATTR_COLD)},
    {6,"浑浊药剂","规则未能解释的混合物",POTION_RARITY_COMMON,0,RULE(0,0,0,0,0,0,0,0,ANY,ANY,ANY,ANY,ANY,ANY,ANY,ANY,0)},
};

void potion_player_defaults(potion_player_t *p){if(p){memset(p,0,sizeof(*p));p->showcase_enabled=1;p->screen_timeout_index=1;}}
const potion_material_def_t *potion_material_def(uint16_t id){return id<POTION_MATERIAL_COUNT?&MATERIALS[id]:NULL;}
const potion_def_t *potion_def(uint16_t id){return id<POTION_TYPE_COUNT?&POTIONS[id]:NULL;}

uint16_t potion_material_for_signal(potion_source_t source,uint64_t fp){uint8_t in[11]={1,1,(uint8_t)source};for(int i=0;i<8;++i)in[3+i]=(uint8_t)(fp>>(56-i*8));uint8_t d[32];potion_sha256(in,sizeof(in),d);unsigned roll=d[0];potion_rarity_t rarity;if(source==POTION_SOURCE_BLE_STABLE)rarity=roll<140?POTION_RARITY_COMMON:roll<222?POTION_RARITY_RARE:roll<250?POTION_RARITY_EPIC:POTION_RARITY_LEGENDARY;else if(source==POTION_SOURCE_BLE_TEMP)rarity=roll<179?POTION_RARITY_COMMON:roll<243?POTION_RARITY_RARE:POTION_RARITY_EPIC;else rarity=roll<184?POTION_RARITY_COMMON:roll<243?POTION_RARITY_RARE:roll<253?POTION_RARITY_EPIC:POTION_RARITY_LEGENDARY;static const uint8_t start[]={0,4,8,10},size[]={4,4,2,2};return start[rarity]+(d[1]%size[rarity]);}

static int64_t fallback_deadline(uint32_t remaining,uint32_t uptime){uint64_t packed=((uint64_t)remaining<<32)|uptime;return -(int64_t)packed;}
static uint32_t original_cooldown(const potion_cooldown_t*c){uint16_t id=potion_material_for_signal((potion_source_t)c->source_type,c->fingerprint);return MATERIALS[id].cooldown_sec;}
static int64_t cooldown_remaining_for(const potion_cooldown_t*c,const potion_clock_t*clock){if(c->next_epoch>0&&clock->epoch_valid)return c->next_epoch>clock->epoch?c->next_epoch-clock->epoch:0;if(c->next_epoch<0){uint64_t packed=(uint64_t)(-c->next_epoch);uint32_t remaining=(uint32_t)(packed>>32),started=(uint32_t)packed;if(c->session_id==clock->session_id){uint32_t elapsed=clock->uptime_sec-started;return elapsed<remaining?(int64_t)(remaining-elapsed):0;}return remaining;}return original_cooldown(c);}
static potion_cooldown_t *cooldown_slot(potion_player_t *p,uint64_t fp,const potion_clock_t*clock,bool create){potion_cooldown_t *free_slot=NULL,*expired_shortest=NULL;uint32_t shortest=UINT32_MAX;for(size_t i=0;i<POTION_COOLDOWN_CAPACITY;++i){potion_cooldown_t*c=&p->cooldowns[i];if(c->used&&c->fingerprint==fp)return c;if(!c->used&&!free_slot)free_slot=c;if(c->used&&clock&&cooldown_remaining_for(c,clock)==0){uint32_t duration=original_cooldown(c);if(!expired_shortest||duration<shortest){expired_shortest=c;shortest=duration;}}}if(!create)return NULL;potion_cooldown_t*slot=free_slot?free_slot:expired_shortest;if(!slot)return NULL;memset(slot,0,sizeof(*slot));return slot;}
int64_t potion_cooldown_remaining(const potion_player_t*p,uint64_t fp,const potion_clock_t*clock){if(!p||!fp||!clock)return 0;for(size_t i=0;i<POTION_COOLDOWN_CAPACITY;++i){const potion_cooldown_t*c=&p->cooldowns[i];if(c->used&&c->fingerprint==fp)return cooldown_remaining_for(c,clock);}return 0;}
bool potion_cooldown_checkpoint(potion_player_t*p,const potion_clock_t*clock){if(!p||!clock)return false;bool changed=false;for(size_t i=0;i<POTION_COOLDOWN_CAPACITY;++i){potion_cooldown_t*c=&p->cooldowns[i];if(!c->used)continue;int64_t remaining=cooldown_remaining_for(c,clock);if(remaining<=0){memset(c,0,sizeof(*c));changed=true;continue;}int64_t deadline=clock->epoch_valid?clock->epoch+remaining:fallback_deadline((uint32_t)remaining,clock->uptime_sec);if(c->next_epoch!=deadline||c->session_id!=clock->session_id){c->next_epoch=deadline;c->session_id=clock->session_id;changed=true;}}return changed;}
bool potion_cooldown_any_active(const potion_player_t*p,const potion_clock_t*clock){if(!p||!clock)return false;for(size_t i=0;i<POTION_COOLDOWN_CAPACITY;++i)if(p->cooldowns[i].used&&cooldown_remaining_for(&p->cooldowns[i],clock)>0)return true;return false;}
bool potion_collect(potion_player_t*p,potion_source_t source,uint64_t fp,uint16_t id,const potion_clock_t*clock,int64_t*remaining){if(!p||id>=POTION_MATERIAL_COUNT||!fp||!clock)return false;int64_t wait=potion_cooldown_remaining(p,fp,clock);if(wait!=0){if(remaining)*remaining=wait;return false;}potion_cooldown_t*c=cooldown_slot(p,fp,clock,true);if(!c||p->materials[id]==UINT16_MAX)return false;p->materials[id]++;c->used=1;c->fingerprint=fp;c->source_type=(uint8_t)source;c->session_id=clock->session_id;c->next_epoch=clock->epoch_valid?clock->epoch+MATERIALS[id].cooldown_sec:fallback_deadline(MATERIALS[id].cooldown_sec,clock->uptime_sec);p->generation++;if(remaining)*remaining=0;return true;}

uint8_t potion_signal_strength_percent(potion_source_t source, int8_t rssi)
{
    /* BLE receivers conventionally report about 10 dB below nearby Wi-Fi APs on
       this board. Map each radio's useful range to one shared proximity scale. */
    int weak = source == POTION_SOURCE_WIFI ? -95 : -100;
    int strong = source == POTION_SOURCE_WIFI ? -45 : -55;
    if (rssi <= weak) return 0;
    if (rssi >= strong) return 100;
    return (uint8_t)(((int)rssi - weak) * 100 / (strong - weak));
}

static uint8_t item_strength(const potion_radar_item_t *item)
{
    return potion_signal_strength_percent(item->source, item->rssi);
}

static int state_rank(int64_t remaining){return remaining==0?2:remaining>0||remaining==-1?0:1;}
size_t potion_radar_aggregate(const potion_player_t*p,const potion_material_signal_t*signals,size_t count,const potion_clock_t*clock,potion_radar_item_t*out,size_t cap,bool exclude_cooling){if(!signals||!out||!cap||!clock)return 0;size_t used=0;for(size_t i=0;i<count;++i){if(signals[i].material_id>=POTION_MATERIAL_COUNT)continue;int64_t rem=potion_cooldown_remaining(p,signals[i].fingerprint,clock);if(exclude_cooling&&rem!=0)continue;size_t at=used;for(size_t j=0;j<used;++j)if(out[j].material_id==signals[i].material_id){at=j;break;}potion_radar_item_t candidate={signals[i].fingerprint,signals[i].rssi,signals[i].material_id,signals[i].source,rem,rem==0};if(at<used){int cr=state_rank(rem),orank=state_rank(out[at].cooldown_remaining);if(cr>orank||(cr==orank&&item_strength(&candidate)>item_strength(&out[at])))out[at]=candidate;}else if(used<cap)out[used++]=candidate;else{size_t weakest=0;for(size_t j=1;j<used;++j)if(item_strength(&out[j])<item_strength(&out[weakest]))weakest=j;if(item_strength(&candidate)>item_strength(&out[weakest]))out[weakest]=candidate;}}for(size_t i=0;i<used;++i)for(size_t j=i+1;j<used;++j)if(item_strength(&out[j])>item_strength(&out[i])){potion_radar_item_t t=out[i];out[i]=out[j];out[j]=t;}return used;}

size_t potion_radar_select(const potion_player_t *player,
                           const potion_material_signal_t *signals, size_t count,
                           const potion_clock_t *clock, potion_radar_item_t *out,
                           size_t cap)
{
    if (!player || !signals || !clock || !out || !cap) return 0;
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!signals[i].fingerprint || signals[i].material_id >= POTION_MATERIAL_COUNT) continue;
        size_t duplicate = used;
        for (size_t j = 0; j < used; ++j) {
            if (out[j].fingerprint == signals[i].fingerprint) {
                duplicate = j;
                break;
            }
        }
        uint8_t strength = potion_signal_strength_percent(signals[i].source, signals[i].rssi);
        if (duplicate < used && strength <= item_strength(&out[duplicate])) continue;
        int64_t remaining = potion_cooldown_remaining(player, signals[i].fingerprint, clock);
        potion_radar_item_t candidate = {
            signals[i].fingerprint, signals[i].rssi, signals[i].material_id,
            signals[i].source, remaining, remaining == 0
        };
        if (duplicate < used) {
            out[duplicate] = candidate;
        } else if (used < cap) {
            out[used++] = candidate;
        } else {
            size_t weakest = 0;
            for (size_t j = 1; j < used; ++j)
                if (item_strength(&out[j]) < item_strength(&out[weakest])) weakest = j;
            if (strength > item_strength(&out[weakest])) out[weakest] = candidate;
        }
    }
    for (size_t i = 0; i < used; ++i) {
        for (size_t j = i + 1; j < used; ++j) {
            if (item_strength(&out[j]) > item_strength(&out[i])) {
                potion_radar_item_t swap = out[i]; out[i] = out[j]; out[j] = swap;
            }
        }
    }
    return used;
}

void potion_explore_reset(potion_explore_progress_t*p){if(p)memset(p,0,sizeof(*p));}
static unsigned proximity_samples(const potion_explore_progress_t*p,uint8_t threshold){unsigned count=0;for(unsigned i=0;i<p->strength_count;++i)count+=p->strength_history[i]>=threshold;return count;}
uint8_t potion_explore_score_at(const potion_explore_progress_t*p,uint8_t threshold){if(!p||!p->initialized)return 0;unsigned prox=proximity_samples(p,threshold);unsigned time_score=p->elapsed_ms>=10000?35:p->elapsed_ms*35/10000,variation_score=p->variation>=6?35:p->variation*35/6,proximity_score=prox>=3?30:prox*10;unsigned score=time_score+variation_score+proximity_score;if(p->last_strength<threshold&&score>90)score=90;return score>100?100:(uint8_t)score;}
uint8_t potion_explore_score(const potion_explore_progress_t*p){return potion_explore_score_at(p,66);}
uint8_t potion_explore_update(potion_explore_progress_t*p,uint64_t fp,uint8_t strength,int8_t raw_rssi,uint32_t elapsed){if(!p||!fp)return 0;if(!p->initialized||p->fingerprint!=fp){potion_explore_reset(p);p->initialized=true;p->fingerprint=fp;p->started_ms=elapsed;p->last_rssi=raw_rssi;}else{unsigned delta=(unsigned)abs((int)raw_rssi-(int)p->last_rssi);p->variation=(uint16_t)(p->variation+delta>UINT16_MAX?UINT16_MAX:p->variation+delta);p->last_rssi=raw_rssi;}p->last_strength=strength;p->elapsed_ms=elapsed-p->started_ms;if(p->sample_count<UINT8_MAX)p->sample_count++;if(p->strength_count<5)p->strength_history[p->strength_count++]=strength;else{memmove(p->strength_history,p->strength_history+1,4);p->strength_history[4]=strength;}return potion_explore_score(p);}
bool potion_explore_collectable_at(const potion_explore_progress_t*p,uint8_t threshold){if(!p||!p->initialized||p->last_strength<threshold)return false;return p->elapsed_ms>=8000&&p->sample_count>=3&&p->variation>=6&&proximity_samples(p,threshold)>=3;}
bool potion_explore_collectable(const potion_explore_progress_t*p){return potion_explore_collectable_at(p,66);}
uint8_t potion_signal_threshold_relax(uint8_t threshold,uint8_t strongest){return strongest<threshold?strongest:threshold;}
uint8_t potion_signal_threshold_recover(uint8_t threshold,uint8_t strength){if(threshold>=66)return 66;unsigned recovered=(unsigned)threshold+4U+(unsigned)strength/12U;return recovered>66?66:(uint8_t)recovered;}

uint32_t potion_duration_ceil(int64_t seconds, potion_duration_unit_t *unit)
{
    uint64_t remaining = seconds > 0 ? (uint64_t)seconds : 0;
    uint64_t divisor = 1;
    potion_duration_unit_t selected = POTION_DURATION_SECONDS;

    if (remaining >= 86400) {
        divisor = 86400;
        selected = POTION_DURATION_DAYS;
    } else if (remaining >= 3600) {
        divisor = 3600;
        selected = POTION_DURATION_HOURS;
    } else if (remaining >= 60) {
        divisor = 60;
        selected = POTION_DURATION_MINUTES;
    }
    if (unit) {
        *unit = selected;
    }
    remaining = (remaining + divisor - 1) / divisor;
    return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}

void potion_radar_layout(const potion_radar_item_t *items, size_t count,
                         potion_radar_point_t *points)
{
    static const int16_t directions[17][2] = {
        {256, 0}, {237, 98}, {181, 181}, {98, 237},
        {0, 256}, {-98, 237}, {-181, 181}, {-237, 98},
        {-256, 0}, {-237, -98}, {-181, -181}, {-98, -237},
        {0, -256}, {98, -237}, {181, -181}, {237, -98},
        {256, 0},
    };
    uint8_t occupied[32] = {0};

    if (!items || !points) {
        return;
    }
    int strongest = count ? item_strength(&items[0]) : 0;
    int weakest = count ? item_strength(&items[count - 1]) : 0;
    int range = strongest - weakest;
    for (size_t i = 0; i < count; ++i) {
        uint64_t mixed = items[i].fingerprint ^ (items[i].fingerprint >> 32)
                       ^ ((uint64_t)items[i].material_id * 0x9e3779b9u);
        unsigned phase = (unsigned)mixed & 255u;
        for (unsigned attempt = 0; attempt < 256 &&
             (occupied[phase >> 3] & (1u << (phase & 7u))); ++attempt) {
            phase = (phase + 73u) & 255u;
        }
        occupied[phase >> 3] |= (uint8_t)(1u << (phase & 7u));
        unsigned sector = phase >> 4;
        unsigned fraction = phase & 15u;
        int x = (directions[sector][0] * (int)(16u - fraction)
               + directions[sector + 1][0] * (int)fraction) / 16;
        int y = (directions[sector][1] * (int)(16u - fraction)
               + directions[sector + 1][1] * (int)fraction) / 16;
        int radius;
        int strength = item_strength(&items[i]);
        if (count == 1) radius = 14;
        else if (range > 0) radius = 14 + (strongest - strength) * 40 / range;
        else radius = 14 + (int)(i * 40 / (count - 1));
        points[i].x = (int8_t)(x * radius / 256);
        points[i].y = (int8_t)(y * radius / 256);
    }
}

void potion_property_total(const uint16_t ids[POTION_CAULDRON_SLOTS],uint8_t totals[POTION_ATTRIBUTE_COUNT]){memset(totals,0,POTION_ATTRIBUTE_COUNT);if(!ids)return;for(size_t s=0;s<POTION_CAULDRON_SLOTS;++s)if(ids[s]<POTION_MATERIAL_COUNT)for(size_t a=0;a<POTION_ATTRIBUTE_COUNT;++a){unsigned v=totals[a]+MATERIALS[ids[s]].properties[a];totals[a]=(uint8_t)(v>UINT8_MAX?UINT8_MAX:v);}}
static bool matches(const potion_def_t*d,const uint8_t t[POTION_ATTRIBUTE_COUNT]){for(size_t a=0;a<POTION_ATTRIBUTE_COUNT;++a){if((d->forbidden_mask&(1u<<a))&&t[a])return false;if(t[a]<d->min_value[a])return false;if(d->max_value[a]!=ANY&&t[a]>d->max_value[a])return false;}return true;}
uint16_t potion_match(const uint16_t ids[POTION_CAULDRON_SLOTS]){uint8_t totals[POTION_ATTRIBUTE_COUNT];potion_property_total(ids,totals);uint16_t best=POTION_MUDDY_ID;for(uint16_t i=0;i<POTION_FORMAL_COUNT;++i)if(matches(&POTIONS[i],totals)&&(best==POTION_MUDDY_ID||POTIONS[i].priority>POTIONS[best].priority||(POTIONS[i].priority==POTIONS[best].priority&&i<best)))best=i;return best;}
static void normalize(uint16_t ids[POTION_CAULDRON_SLOTS]){for(size_t i=1;i<POTION_CAULDRON_SLOTS;++i){uint16_t v=ids[i];size_t j=i;while(j&&ids[j-1]>v){ids[j]=ids[j-1];--j;}ids[j]=v;}}
static void history_touch(potion_player_t*p,uint16_t potion_id,const uint16_t input[POTION_CAULDRON_SLOTS]){uint16_t ids[POTION_CAULDRON_SLOTS];memcpy(ids,input,sizeof(ids));normalize(ids);potion_recipe_history_t*list=p->history[potion_id];size_t found=POTION_HISTORY_PER_TYPE,empty=POTION_HISTORY_PER_TYPE,oldest=0;uint32_t oldest_gen=UINT32_MAX;for(size_t i=0;i<POTION_HISTORY_PER_TYPE;++i){if(list[i].used&&memcmp(list[i].material_id,ids,sizeof(ids))==0)found=i;if(!list[i].used&&empty==POTION_HISTORY_PER_TYPE)empty=i;if(list[i].used&&list[i].last_used_generation<oldest_gen){oldest_gen=list[i].last_used_generation;oldest=i;}}size_t at=found<POTION_HISTORY_PER_TYPE?found:(empty<POTION_HISTORY_PER_TYPE?empty:oldest);memcpy(list[at].material_id,ids,sizeof(ids));list[at].last_used_generation=p->generation;list[at].used=1;}
bool potion_brew_candidate(const potion_player_t*p,const uint16_t ids[POTION_CAULDRON_SLOTS],potion_player_t*candidate,uint16_t*out_id,uint32_t*out_seed){if(!p||!candidate||!ids||!p->lab_set)return false;uint16_t needed[POTION_MATERIAL_COUNT]={0};for(size_t i=0;i<POTION_CAULDRON_SLOTS;++i){if(ids[i]>=POTION_MATERIAL_COUNT||needed[ids[i]]==UINT16_MAX)return false;needed[ids[i]]++;}for(size_t i=0;i<POTION_MATERIAL_COUNT;++i)if(p->materials[i]<needed[i])return false;uint16_t id=potion_match(ids);if(p->potions[id]==UINT16_MAX)return false;*candidate=*p;for(size_t i=0;i<POTION_MATERIAL_COUNT;++i)candidate->materials[i]-=needed[i];candidate->potions[id]++;candidate->generation++;uint8_t in[18]={(uint8_t)id,(uint8_t)(id>>8)};for(int i=0;i<8;++i)in[2+i]=(uint8_t)(p->lab_fingerprint>>(56-i*8));uint16_t sorted[4];memcpy(sorted,ids,sizeof(sorted));normalize(sorted);for(size_t i=0;i<4;++i){in[10+i*2]=(uint8_t)sorted[i];in[11+i*2]=(uint8_t)(sorted[i]>>8);}uint8_t d[32];potion_sha256(in,sizeof(in),d);uint32_t seed=((uint32_t)d[0]<<24)|((uint32_t)d[1]<<16)|((uint32_t)d[2]<<8)|d[3];candidate->potion_seed[id]=seed;history_touch(candidate,id,ids);if(out_id)*out_id=id;if(out_seed)*out_seed=seed;return true;}
size_t potion_recipe_history_count(const potion_player_t*p,uint16_t id){size_t n=0;if(p&&id<POTION_TYPE_COUNT)for(size_t i=0;i<POTION_HISTORY_PER_TYPE;++i)n+=p->history[id][i].used;return n;}
const potion_recipe_history_t*potion_recipe_history_at(const potion_player_t*p,uint16_t id,size_t recent){if(!p||id>=POTION_TYPE_COUNT)return NULL;const potion_recipe_history_t*best=NULL;uint32_t ceiling=UINT32_MAX;for(size_t rank=0;rank<=recent;++rank){best=NULL;uint32_t best_gen=0;for(size_t i=0;i<POTION_HISTORY_PER_TYPE;++i){const potion_recipe_history_t*e=&p->history[id][i];if(e->used&&e->last_used_generation<ceiling&&(!best||e->last_used_generation>best_gen)){best=e;best_gen=e->last_used_generation;}}if(!best)return NULL;ceiling=best_gen;}return best;}
size_t potion_owned_material_types(const potion_player_t*p){size_t n=0;if(p)for(size_t i=0;i<POTION_MATERIAL_COUNT;++i)n+=p->materials[i]>0;return n;}
size_t potion_owned_potion_types(const potion_player_t*p){size_t n=0;if(p)for(size_t i=0;i<POTION_TYPE_COUNT;++i)n+=p->potions[i]>0;return n;}
void potion_idle_init(potion_idle_t*idle,uint32_t now){if(idle)*idle=(potion_idle_t){.last_activity_ms=now};}
bool potion_idle_note_input(potion_idle_t*idle,uint32_t now){if(!idle)return false;bool woke=idle->screen_off;idle->screen_off=false;idle->last_activity_ms=now;return woke;}
bool potion_idle_poll(potion_idle_t*idle,uint32_t now,uint32_t timeout){if(!idle||idle->screen_off||!timeout)return false;if((uint32_t)(now-idle->last_activity_ms)<timeout)return false;idle->screen_off=true;return true;}
