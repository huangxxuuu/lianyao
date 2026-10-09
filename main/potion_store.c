#include "potion_store.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define STORE_MAGIC 0x5050474dU
#define STORE_SCHEMA 3U
#define STORE_PARTITION "potion_save"
#define STORE_NAMESPACE "potion_game"

typedef struct {
    uint32_t magic;
    uint16_t schema;
    uint16_t size;
    potion_player_t player;
    uint32_t crc32;
} store_blob_t;

typedef struct {
    uint16_t materials[12];
    uint16_t potions[6];
    uint32_t potion_seed[6];
    potion_cooldown_t cooldowns[48];
    uint64_t lab_fingerprint;
    uint32_t generation;
    uint8_t lab_set;
    uint8_t showcase_enabled;
} schema1_player_t;

typedef struct {
    uint32_t magic;
    uint16_t schema;
    uint16_t size;
    schema1_player_t player;
    uint32_t crc32;
} schema1_blob_t;

typedef struct {
    uint16_t materials[POTION_MATERIAL_COUNT];
    uint16_t potions[POTION_TYPE_COUNT];
    uint32_t potion_seed[POTION_TYPE_COUNT];
    potion_recipe_history_t history[POTION_TYPE_COUNT][POTION_HISTORY_PER_TYPE];
    potion_cooldown_t cooldowns[POTION_COOLDOWN_CAPACITY];
    uint64_t lab_fingerprint;
    uint32_t generation;
    uint8_t lab_set;
    uint8_t showcase_enabled;
} schema2_player_t;

typedef struct {
    uint32_t magic;
    uint16_t schema;
    uint16_t size;
    schema2_player_t player;
    uint32_t crc32;
} schema2_blob_t;

static const char *TAG = "potion_store";
static nvs_handle_t s_nvs;
static bool s_ready;
static bool s_error;

static uint32_t crc32_bytes(const void *data,size_t len){const uint8_t*p=data;uint32_t crc=0xffffffffU;for(size_t i=0;i<len;++i){crc^=p[i];for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320U&(uint32_t)-(int32_t)(crc&1U));}return ~crc;}

static bool read_current(nvs_handle_t handle,const char *key,potion_player_t *player){store_blob_t *b=malloc(sizeof(*b));if(!b)return false;size_t size=sizeof(*b);bool ok=nvs_get_blob(handle,key,b,&size)==ESP_OK&&size==sizeof(*b)&&b->magic==STORE_MAGIC&&b->schema==STORE_SCHEMA&&b->size==sizeof(*b)&&b->crc32==crc32_bytes(b,offsetof(store_blob_t,crc32));if(ok)*player=b->player;free(b);return ok;}

static bool read_schema1(nvs_handle_t handle,const char *key,potion_player_t *player){schema1_blob_t *b=malloc(sizeof(*b));if(!b)return false;size_t size=sizeof(*b);bool ok=nvs_get_blob(handle,key,b,&size)==ESP_OK&&size==sizeof(*b)&&b->magic==STORE_MAGIC&&b->schema==1&&b->size==sizeof(*b)&&b->crc32==crc32_bytes(b,offsetof(schema1_blob_t,crc32));if(ok){potion_player_defaults(player);memcpy(player->materials,b->player.materials,sizeof(b->player.materials));memcpy(player->potions,b->player.potions,sizeof(b->player.potions));memcpy(player->potion_seed,b->player.potion_seed,sizeof(b->player.potion_seed));memcpy(player->cooldowns,b->player.cooldowns,sizeof(b->player.cooldowns));player->lab_fingerprint=b->player.lab_fingerprint;player->generation=b->player.generation;player->lab_set=b->player.lab_set;player->showcase_enabled=b->player.showcase_enabled;}free(b);return ok;}

static bool read_schema2(nvs_handle_t handle,const char *key,potion_player_t *player){schema2_blob_t *b=malloc(sizeof(*b));if(!b)return false;size_t size=sizeof(*b);bool ok=nvs_get_blob(handle,key,b,&size)==ESP_OK&&size==sizeof(*b)&&b->magic==STORE_MAGIC&&b->schema==2&&b->size==sizeof(*b)&&b->crc32==crc32_bytes(b,offsetof(schema2_blob_t,crc32));if(ok){potion_player_defaults(player);memcpy(player->materials,b->player.materials,sizeof(b->player.materials));memcpy(player->potions,b->player.potions,sizeof(b->player.potions));memcpy(player->potion_seed,b->player.potion_seed,sizeof(b->player.potion_seed));memcpy(player->history,b->player.history,sizeof(b->player.history));memcpy(player->cooldowns,b->player.cooldowns,sizeof(b->player.cooldowns));player->lab_fingerprint=b->player.lab_fingerprint;player->generation=b->player.generation;player->lab_set=b->player.lab_set;player->showcase_enabled=b->player.showcase_enabled;}free(b);return ok;}

static bool load_best(nvs_handle_t handle,potion_player_t *player,potion_player_t *candidate){bool found=false;if(read_current(handle,"state0",candidate)||read_schema2(handle,"state0",candidate)||read_schema1(handle,"state0",candidate)){*player=*candidate;found=true;}if((read_current(handle,"state1",candidate)||read_schema2(handle,"state1",candidate)||read_schema1(handle,"state1",candidate))&&(!found||candidate->generation>=player->generation)){*player=*candidate;found=true;}return found;}

static bool write_player(const potion_player_t *player){store_blob_t *b=calloc(1,sizeof(*b));if(!b)return false;b->magic=STORE_MAGIC;b->schema=STORE_SCHEMA;b->size=sizeof(*b);b->player=*player;b->crc32=crc32_bytes(b,offsetof(store_blob_t,crc32));const char *key=(player->generation&1U)?"state1":"state0";esp_err_t err=nvs_set_blob(s_nvs,key,b,sizeof(*b));if(err==ESP_OK)err=nvs_commit(s_nvs);free(b);if(err!=ESP_OK)ESP_LOGE(TAG,"save failed: %s",esp_err_to_name(err));return err==ESP_OK;}

static int migrate_legacy(potion_player_t *player,potion_player_t *candidate){esp_err_t err=nvs_flash_init();if(err!=ESP_OK){ESP_LOGW(TAG,"legacy NVS unavailable: %s",esp_err_to_name(err));return 0;}nvs_handle_t legacy=0;err=nvs_open(STORE_NAMESPACE,NVS_READONLY,&legacy);if(err!=ESP_OK)return 0;bool found=load_best(legacy,player,candidate);nvs_close(legacy);if(!found)return 0;if(!write_player(player)){ESP_LOGE(TAG,"legacy save migration failed");return-1;}ESP_LOGI(TAG,"migrated legacy save generation %lu",(unsigned long)player->generation);return 1;}

bool potion_store_init(potion_player_t *player){if(!player)return false;potion_player_defaults(player);esp_err_t err=nvs_flash_init_partition(STORE_PARTITION);if(err!=ESP_OK){ESP_LOGE(TAG,"save partition init failed: %s; refusing to erase user data",esp_err_to_name(err));s_error=true;return false;}err=nvs_open_from_partition(STORE_PARTITION,STORE_NAMESPACE,NVS_READWRITE,&s_nvs);if(err!=ESP_OK){ESP_LOGE(TAG,"save partition open failed: %s",esp_err_to_name(err));s_error=true;return false;}potion_player_t*candidate=malloc(sizeof(*candidate));if(!candidate){ESP_LOGE(TAG,"not enough memory to load player state");s_error=true;nvs_close(s_nvs);return false;}bool has_player=load_best(s_nvs,player,candidate);if(!has_player){int migrated=migrate_legacy(player,candidate);if(migrated<0){free(candidate);s_error=true;nvs_close(s_nvs);return false;}has_player=migrated>0;}free(candidate);s_ready=true;ESP_LOGI(TAG,"restored generation %lu from %s (state %u bytes)",(unsigned long)player->generation,has_player?"saved state":"defaults",(unsigned)sizeof(*player));return true;}

bool potion_store_request_save(const potion_player_t *player){if(!s_ready||!player)return false;if(!write_player(player)){s_error=true;return false;}return true;}

bool potion_store_has_error(void){return s_error;}
