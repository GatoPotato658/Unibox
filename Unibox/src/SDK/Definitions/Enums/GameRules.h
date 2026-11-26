#pragma once

enum HalloweenBossType
{
	HALLOWEEN_BOSS_INVALID = 0,
	HALLOWEEN_BOSS_HHH = 1,
	HALLOWEEN_BOSS_MONOCULUS = 2,
	HALLOWEEN_BOSS_MERASMUS = 3
};

enum PowerupBottleType_t
{
	POWERUP_BOTTLE_NONE,
	POWERUP_BOTTLE_CRITBOOST,
	POWERUP_BOTTLE_UBERCHARGE,
	POWERUP_BOTTLE_RECALL,
	POWERUP_BOTTLE_REFILL_AMMO,
	POWERUP_BOTTLE_BUILDINGS_INSTANT_UPGRADE,
	POWERUP_BOTTLE_RADIUS_STEALTH,
	POWERUP_BOTTLE_TOTAL
};

enum ETFFlagType
{
	TF_FLAGTYPE_CTF = 0,
	TF_FLAGTYPE_ATTACK_DEFEND,
	TF_FLAGTYPE_TERRITORY_CONTROL,
	TF_FLAGTYPE_INVADE,
	TF_FLAGTYPE_RESOURCE_CONTROL,
	TF_FLAGTYPE_ROBOT_DESTRUCTION,
	TF_FLAGTYPE_PLAYER_DESTRUCTION
};

enum ETFGameType
{
	TF_GAMETYPE_UNDEFINED = 0,
	TF_GAMETYPE_CTF,
	TF_GAMETYPE_CP,
	TF_GAMETYPE_ESCORT,
	TF_GAMETYPE_ARENA,
	TF_GAMETYPE_MVM,
	TF_GAMETYPE_RD,
	TF_GAMETYPE_PASSTIME,
	TF_GAMETYPE_PD,
	TF_GAMETYPE_COUNT
};

enum gamerules_roundstate_t
{
	// initialize the game, create teams
	GR_STATE_INIT = 0,

	//Before players have joined the game. Periodically checks to see if enough players are ready
	//to start a game. Also reverts to this when there are no active players
	GR_STATE_PREGAME,

	//The game is about to start, wait a bit and spawn everyone
	GR_STATE_STARTGAME,

	//All players are respawned, frozen in place
	GR_STATE_PREROUND,

	//Round is on, playing normally
	GR_STATE_RND_RUNNING,

	//Someone has won the round
	GR_STATE_TEAM_WIN,

	//Noone has won, manually restart the game, reset scores
	GR_STATE_RESTART,

	//Noone has won, restart the game
	GR_STATE_STALEMATE,

	//Game is over, showing the scoreboard etc
	GR_STATE_GAME_OVER,

	//Game is in a bonus state, transitioned to after a round ends
	GR_STATE_BONUS,

	//Game is awaiting the next wave/round of a multi round experience
	GR_STATE_BETWEEN_RNDS,

	GR_NUM_ROUND_STATES
};

enum
{
	TF_ARENA_NOTIFICATION_CAREFUL = 0,
	TF_ARENA_NOTIFICATION_SITOUT,
	TF_ARENA_NOTIFICATION_NOPLAYERS
};

enum
{
	MVM_EVENT_POPFILE_NONE = 0,
	MVM_EVENT_POPFILE_HALLOWEEN,
	MVM_EVENT_POPFILE_MAX_TYPES
};

enum
{
	DRAW_ARROW_UP,
	DRAW_ARROW_LEFT,
	DRAW_ARROW_RIGHT
};

enum
{
	TF_HUDTYPE_UNDEFINED = 0,
	TF_HUDTYPE_CTF,
	TF_HUDTYPE_CP,
	TF_HUDTYPE_ESCORT,
	TF_HUDTYPE_ARENA,
	TF_HUDTYPE_TRAINING
};

enum
{
	TF_BUILDING_SENTRY = (1 << 0),
	TF_BUILDING_DISPENSER = (1 << 1),
	TF_BUILDING_TELEPORT = (1 << 2)
};

enum
{
	TF_ITEM_UNDEFINED = 0,
	TF_ITEM_CAPTURE_FLAG = (1 << 0),
	TF_ITEM_HEALTH_KIT = (1 << 1),
	TF_ITEM_ARMOR = (1 << 2),
	TF_ITEM_AMMO_PACK = (1 << 3),
	TF_ITEM_GRENADE_PACK = (1 << 4)
};

enum ETFFlagEventTypes
{
	TF_FLAGEVENT_PICKUP = 1,
	TF_FLAGEVENT_CAPTURE,
	TF_FLAGEVENT_DEFEND,
	TF_FLAGEVENT_DROPPED,
	TF_FLAGEVENT_RETURNED,
	TF_NUM_FLAG_EVENTS
};

enum TauntAttack_t
{
	TAUNTATK_NONE = 0,
	TAUNTATK_PYRO_HADOUKEN,
	TAUNTATK_HEAVY_EAT,
	TAUNTATK_HEAVY_RADIAL_BUFF,
	TAUNTATK_HEAVY_HIGH_NOON,
	TAUNTATK_SCOUT_DRINK,
	TAUNTATK_SCOUT_GRAND_SLAM,
	TAUNTATK_MEDIC_INHALE,
	TAUNTATK_SPY_FENCING_SLASH_A,
	TAUNTATK_SPY_FENCING_SLASH_B,
	TAUNTATK_SPY_FENCING_STAB,
	TAUNTATK_RPS_KILL,
	TAUNTATK_SNIPER_ARROW_STAB_IMPALE,
	TAUNTATK_SNIPER_ARROW_STAB_KILL,
	TAUNTATK_SOLDIER_GRENADE_KILL,
	TAUNTATK_DEMOMAN_BARBARIAN_SWING,
	TAUNTATK_MEDIC_UBERSLICE_IMPALE,
	TAUNTATK_MEDIC_UBERSLICE_KILL,
	TAUNTATK_FLIP_LAND_PARTICLE,
	TAUNTATK_RPS_PARTICLE,
	TAUNTATK_HIGHFIVE_PARTICLE,
	TAUNTATK_ENGINEER_GUITAR_SMASH,
	TAUNTATK_ENGINEER_ARM_IMPALE,
	TAUNTATK_ENGINEER_ARM_KILL,
	TAUNTATK_ENGINEER_ARM_BLEND,
	TAUNTATK_SOLDIER_GRENADE_KILL_WORMSIGN,
	TAUNTATK_SHOW_ITEM,
	TAUNTATK_MEDIC_RELEASE_DOVES,
	TAUNTATK_PYRO_ARMAGEDDON,
	TAUNTATK_PYRO_SCORCHSHOT,
	TAUNTATK_ALLCLASS_GUITAR_RIFF,
	TAUNTATK_MEDIC_HEROIC_TAUNT,
	TAUNTATK_COUNT
};

enum ObjectType_t
{
	OBJ_DISPENSER = 0,
	OBJ_TELEPORTER,
	OBJ_SENTRYGUN,
	OBJ_ATTACHMENT_SAPPER,
	OBJ_LAST
};

typedef enum
{
	BUILDING_HUD_ALERT_NONE = 0,
	BUILDING_HUD_ALERT_LOW_AMMO,
	BUILDING_HUD_ALERT_LOW_HEALTH,
	BUILDING_HUD_ALERT_VERY_LOW_AMMO,
	BUILDING_HUD_ALERT_VERY_LOW_HEALTH,
	BUILDING_HUD_ALERT_SAPPER,
	MAX_BUILDING_HUD_ALERT_LEVEL
} BuildingHudAlert_t;

typedef enum
{
	BUILDING_DAMAGE_LEVEL_NONE = 0,
	BUILDING_DAMAGE_LEVEL_LIGHT,
	BUILDING_DAMAGE_LEVEL_MEDIUM,
	BUILDING_DAMAGE_LEVEL_HEAVY,
	BUILDING_DAMAGE_LEVEL_CRITICAL,
	MAX_BUILDING_DAMAGE_LEVEL
} BuildingDamageLevel_t;

enum
{
	MODE_TELEPORTER_ENTRANCE = 0,
	MODE_TELEPORTER_EXIT
};

enum
{
	MODE_SENTRYGUN_NORMAL = 0,
	MODE_SENTRYGUN_DISPOSABLE
};

enum
{
	MODE_SAPPER_NORMAL = 0,
	MODE_SAPPER_ANTI_ROBOT,
	MODE_SAPPER_ANTI_ROBOT_RADIUS
};

enum ESpyTrapType_t
{
	MODE_SPY_TRAP_RADIUS_STEALTH = 0,
	MODE_SPY_TRAP_REPROGRAM,
	MODE_SPY_TRAP_MAGNET
};

enum
{
	DISPENSER_STATE_IDLE,
	DISPENSER_STATE_UPGRADING
};

enum
{
	TELEPORTER_STATE_BUILDING = 0,
	TELEPORTER_STATE_IDLE,
	TELEPORTER_STATE_READY,
	TELEPORTER_STATE_SENDING,
	TELEPORTER_STATE_RECEIVING,
	TELEPORTER_STATE_RECEIVING_RELEASE,
	TELEPORTER_STATE_RECHARGING,
	TELEPORTER_STATE_UPGRADING,
};
enum
{
	SENTRY_STATE_INACTIVE = 0,
	SENTRY_STATE_SEARCHING,
	SENTRY_STATE_ATTACKING,
	SENTRY_STATE_UPGRADING,
	SENTRY_NUM_STATES
};

enum
{
	OF_ALLOW_REPEAT_PLACEMENT = 0x01,
	OF_MUST_BE_BUILT_ON_ATTACHMENT = 0x02,
	OF_DOESNT_HAVE_A_MODEL = 0x04,
	OF_PLAYER_DESTRUCTION = 0x08,
	OF_BIT_COUNT = 4
};

enum
{
	BS_IDLE = 0,
	BS_SELECTING,
	BS_PLACING,
	BS_PLACING_INVALID
};

enum
{
	BUILDER_OBJECT_BITS = 8,
	BUILDER_INVALID_OBJECT = ((1 << BUILDER_OBJECT_BITS) - 1)
};

enum CurrencyRewards_t
{
	TF_CURRENCY_KILLED_PLAYER,
	TF_CURRENCY_KILLED_OBJECT,
	TF_CURRENCY_ASSISTED_PLAYER,
	TF_CURRENCY_BONUS_POINTS,
	TF_CURRENCY_CAPTURED_OBJECTIVE,
	TF_CURRENCY_ESCORT_REWARD,
	TF_CURRENCY_PACK_SMALL,
	TF_CURRENCY_PACK_MEDIUM,
	TF_CURRENCY_PACK_LARGE,
	TF_CURRENCY_PACK_CUSTOM,
	TF_CURRENCY_TIME_REWARD,
	TF_CURRENCY_WAVE_COLLECTION_BONUS
};

enum MVMAnnouncement_t
{
	TF_MVM_ANNOUNCEMENT_WAVE_COMPLETE,
	TF_MVM_ANNOUNCEMENT_WAVE_FAILED,
	TF_MVM_ANNOUNCEMENT_TOTAL
};

enum MVMUpgradeUIGroups_t
{
	UIGROUP_UPGRADE_ATTACHED_TO_ITEM = 0,
	UIGROUP_UPGRADE_ATTACHED_TO_PLAYER,
	UIGROUP_POWERUPBOTTLE
};

enum
{
	MVM_UPGRADE_QUALITY_LOW = 1,
	MVM_UPGRADE_QUALITY_NORMAL,
	MVM_UPGRADE_QAULITY_HIGH
};

enum
{
	TF_SAPEVENT_NONE = 0,
	TF_SAPEVENT_PLACED,
	TF_SAPEVENT_DONE
};

enum RuneTypes_t
{
	RUNE_NONE = -1,
	RUNE_STRENGTH,
	RUNE_HASTE,
	RUNE_REGEN,
	RUNE_RESIST,
	RUNE_VAMPIRE,
	RUNE_REFLECT,
	RUNE_PRECISION,
	RUNE_AGILITY,
	RUNE_KNOCKOUT,
	RUNE_KING,
	RUNE_PLAGUE,
	RUNE_SUPERNOVA,
	RUNE_TYPES_MAX
};

enum ETFMatchGroup
{
	k_eTFMatchGroup_Invalid = -1,
	k_eTFMatchGroup_MvM_Practice = 0,
	k_eTFMatchGroup_MvM_MannUp = 1,
	k_eTFMatchGroup_First = 0,
	k_eTFMatchGroup_MvM_Default = 0,
	k_eTFMatchGroup_MvM_First = 0,
	k_eTFMatchGroup_MvM_Last = 1,
	k_eTFMatchGroup_Ladder_6v6 = 2,
	k_eTFMatchGroup_Ladder_9v9 = 3,
	k_eTFMatchGroup_Ladder_12v12 = 4,
	k_eTFMatchGroup_Ladder_Default = 2,
	k_eTFMatchGroup_Ladder_First = 2,
	k_eTFMatchGroup_Ladder_Last = 4,
	k_eTFMatchGroup_Casual_6v6 = 5,
	k_eTFMatchGroup_Casual_9v9 = 6,
	k_eTFMatchGroup_Casual_12v12 = 7,
	k_eTFMatchGroup_Casual_Default = 7,
	k_eTFMatchGroup_Casual_First = 5,
	k_eTFMatchGroup_Casual_Last = 7,
	k_eTFMatchGroup_Event_Placeholder = 8,
	k_eTFMatchGroup_Event_Default = 8,
	k_eTFMatchGroup_Event_First = 8,
	k_eTFMatchGroup_Event_Last = 8
};

enum
{
	PARTITION_ENGINE_SOLID_EDICTS = (1 << 0),
	PARTITION_ENGINE_TRIGGER_EDICTS = (1 << 1),
	PARTITION_CLIENT_SOLID_EDICTS = (1 << 2),
	PARTITION_CLIENT_RESPONSIVE_EDICTS = (1 << 3),
	PARTITION_ENGINE_NON_STATIC_EDICTS = (1 << 4),
	PARTITION_CLIENT_STATIC_PROPS = (1 << 5),
	PARTITION_ENGINE_STATIC_PROPS = (1 << 6),
	PARTITION_CLIENT_NON_STATIC_EDICTS = (1 << 7)
};

enum soundlevel_t
{
	SNDLVL_NONE = 0,
	SNDLVL_20dB = 20,
	SNDLVL_25dB = 25,
	SNDLVL_30dB = 30,
	SNDLVL_35dB = 35,
	SNDLVL_40dB = 40,
	SNDLVL_45dB = 45,
	SNDLVL_50dB = 50,
	SNDLVL_55dB = 55,
	SNDLVL_IDLE = 60,
	SNDLVL_60dB = 60,
	SNDLVL_65dB = 65,
	SNDLVL_STATIC = 66,
	SNDLVL_70dB = 70,
	SNDLVL_NORM = 75,
	SNDLVL_75dB = 75,
	SNDLVL_80dB = 80,
	SNDLVL_TALKING = 80,
	SNDLVL_85dB = 85,
	SNDLVL_90dB = 90,
	SNDLVL_95dB = 95,
	SNDLVL_100dB = 100,
	SNDLVL_105dB = 105,
	SNDLVL_110dB = 110,
	SNDLVL_120dB = 120,
	SNDLVL_130dB = 130,
	SNDLVL_GUNFIRE = 140,
	SNDLVL_140dB = 140,
	SNDLVL_150dB = 150,
	SNDLVL_180dB = 180
};

enum
{
	WL_NotInWater = 0,
	WL_Feet,
	WL_Waist,
	WL_Eyes
};

class CAudioSource;
class CSfxTable
{
public:
	CSfxTable();

	virtual const char	*getname();
	const char			*GetFileName();
	void*				GetFileNameHandle();
	void				SetNamePoolIndex( int index );
	bool				IsPrecachedSound();
	void				OnNameChanged( const char *pName );
	int					m_namePoolIndex;
	CAudioSource		*pSource;
	bool				m_bUseErrorFilename : 1;
	bool				m_bIsUISound : 1;
	bool				m_bIsLateLoad : 1;
	bool				m_bMixGroupsCached : 1;
	byte				m_mixGroupCount;
	byte				m_mixGroupList[8];

private:
	const char *m_pDebugName;
};

struct StartSoundParams_t
{
	bool			staticsound;
	int				userdata;
	int				soundsource;
	int				entchannel;
	CSfxTable*		pSfx;
	Vec3			origin;
	Vec3			direction;
	bool			bUpdatePositions;
	float			fvol;
	soundlevel_t	soundlevel;
	int				flags;
	int				pitch;
	int				specialdsp;
	bool			fromserver;
	float			delay;
	int				speakerentity;
	bool			suppressrecording;
	int				initialStreamPosition;
};

class CParticleSubTexture;
struct Particle
{
	Particle *m_pPrev, *m_pNext;

	// Which sub texture this particle uses (so we can get at the tcoord mins and maxs).
	CParticleSubTexture *m_pSubTexture;

	// If m_Pos isn't used to store the world position, then implement IParticleEffect::GetParticlePosition()
	Vector m_Pos;			// Position of the particle in world space
};

class SimpleParticle : public Particle
{
public:
	SimpleParticle() : m_iFlags(0) {}

	// AddSimpleParticle automatically initializes these fields.
	Vector		m_vecVelocity;
	float		m_flRoll;
	float		m_flDieTime;	// How long it lives for.
	float		m_flLifetime;	// How long it has been alive for so far.
	unsigned char	m_uchColor[3];
	unsigned char	m_uchStartAlpha;
	unsigned char	m_uchEndAlpha;
	unsigned char	m_uchStartSize;
	unsigned char	m_uchEndSize;
	unsigned char 	m_iFlags;
	float		m_flRollDelta;
};

// Crafting slots on crafting page
#define CRAFTING_SLOTS_INPUT_ROWS					3
#define CRAFTING_SLOTS_INPUT_COLUMNS				4
#define CRAFTING_SLOTS_INPUTPANELS					(CRAFTING_SLOTS_INPUT_ROWS * CRAFTING_SLOTS_INPUT_COLUMNS)

class CCraftingPanel
{
public:
	// Not sure if the paddings are correct
    byte pad0[808];
    // Items in the input model panels
    unsigned long long m_InputItems[CRAFTING_SLOTS_INPUTPANELS];
    const int *m_ItemPanelCriteria[CRAFTING_SLOTS_INPUTPANELS];

    byte pad1[128];
    int m_iCurrentlySelectedRecipe;

    byte pad2[68];

    int m_iCraftingAttempts;
};
