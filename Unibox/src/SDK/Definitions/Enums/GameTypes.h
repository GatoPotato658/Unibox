#pragma once

enum MinigunState_t
{
	AC_STATE_IDLE = 0,
	AC_STATE_STARTFIRING,
	AC_STATE_FIRING,
	AC_STATE_SPINNING,
	AC_STATE_DRYFIRE
};

enum minigun_weapontypes_t
{
	MINIGUN_STANDARD = 0,
	MINIGUN_STUN
};

typedef enum
{
	GROUND = 0,
	STUCK,
	LADDER
} IntervalType_t;

enum
{
	SPEED_CROPPED_RESET = 0,
	SPEED_CROPPED_DUCK = 1,
	SPEED_CROPPED_WEAPON = 2
};

typedef enum
{
	PREDICTION_SIMULATION_RESULTS_ARRIVING_ON_SEND_FRAME = 0,
	PREDICTION_NORMAL
} PREDICTION_REASON;

enum view_id_t
{
	VIEW_ILLEGAL = -2,
	VIEW_NONE = -1,
	VIEW_MAIN = 0,
	VIEW_3DSKY = 1,
	VIEW_MONITOR = 2,
	VIEW_REFLECTION = 3,
	VIEW_REFRACTION = 4,
	VIEW_INTRO_PLAYER = 5,
	VIEW_INTRO_CAMERA = 6,
	VIEW_SHADOW_DEPTH_TEXTURE = 7,
	VIEW_SSAO = 8,
	VIEW_ID_COUNT
};

enum medigun_weapontypes_t
{
	MEDIGUN_STANDARD = 0,
	MEDIGUN_UBER,
	MEDIGUN_QUICKFIX,
	MEDIGUN_RESIST
};

enum RifleTypes_t
{
	RIFLE_NORMAL = 0,
	RIFLE_JARATE,
	RIFLE_MACHINA,
	RIFLE_CLASSIC,
};

enum medigun_resist_types_t
{
	MEDIGUN_BULLET_RESIST = 0,
	MEDIGUN_BLAST_RESIST,
	MEDIGUN_FIRE_RESIST,
	MEDIGUN_NUM_RESISTS
};

enum DetonateTypes_t
{
	TF_DETONATE_MODE_STANDARD = 0,
	TF_DETONATE_MODE_DOT,
	TF_DETONATE_MODE_AIR,
};

enum FlareGunTypes_t
{
	FLAREGUN_NORMAL = 0,
	FLAREGUN_DETONATE,
	FLAREGUN_GRORDBORT,
	FLAREGUN_SCORCHSHOT
};

enum PrecipitationType_t
{
	PRECIPITATION_TYPE_RAIN = 0,
	PRECIPITATION_TYPE_SNOW,
	PRECIPITATION_TYPE_ASH,
	PRECIPITATION_TYPE_SNOWFALL,
	NUM_PRECIPITATION_TYPES
};

class CViewVectors
{
public:
	Vector m_vView;
	Vector m_vHullMin;
	Vector m_vHullMax;
	Vector m_vDuckHullMin;
	Vector m_vDuckHullMax;
	Vector m_vDuckView;
	Vector m_vObsHullMin;
	Vector m_vObsHullMax;
	Vector m_vDeadViewHeight;
};

typedef enum
{
	VOTE_FAILED_GENERIC = 0,
	VOTE_FAILED_TRANSITIONING_PLAYERS,
	VOTE_FAILED_RATE_EXCEEDED,
	VOTE_FAILED_YES_MUST_EXCEED_NO,
	VOTE_FAILED_QUORUM_FAILURE,
	VOTE_FAILED_ISSUE_DISABLED,
	VOTE_FAILED_MAP_NOT_FOUND,
	VOTE_FAILED_MAP_NAME_REQUIRED,
	VOTE_FAILED_ON_COOLDOWN,
	VOTE_FAILED_TEAM_CANT_CALL,
	VOTE_FAILED_WAITINGFORPLAYERS,
	VOTE_FAILED_PLAYERNOTFOUND,
	VOTE_FAILED_CANNOT_KICK_ADMIN,
	VOTE_FAILED_SCRAMBLE_IN_PROGRESS,
	VOTE_FAILED_SPECTATOR,
	VOTE_FAILED_NEXTLEVEL_SET,
	VOTE_FAILED_MAP_NOT_VALID,
	VOTE_FAILED_CANNOT_KICK_FOR_TIME,
	VOTE_FAILED_CANNOT_KICK_DURING_ROUND,
	VOTE_FAILED_VOTE_IN_PROGRESS,
	VOTE_FAILED_KICK_LIMIT_REACHED,
	VOTE_FAILED_KICK_DENIED_BY_GC,
	VOTE_FAILED_MODIFICATION_ALREADY_ACTIVE
} vote_create_failed_t;

enum UserMessageType
{
	Geiger = 0,
	Train,
	HudText,
	SayText,
	SayText2,
	TextMsg,
	ResetHUD,
	GameTitle,
	ItemPickup,
	ShowMenu,
	Shake,
	Fade,
	VGUIMenu,
	Rumble,
	CloseCaption,
	SendAudio,
	VoiceMask,
	RequestState,
	Damage,
	HintText,
	KeyHintText,
	HudMsg,
	AmmoDenied,
	AchievementEvent,
	UpdateRadar,
	VoiceSubtitle,
	HudNotify,
	HudNotifyCustom,
	PlayerStatsUpdate,
	MapStatsUpdate,
	PlayerIgnited,
	PlayerIgnitedInv,
	HudArenaNotify,
	UpdateAchievement,
	TrainingMsg,
	TrainingObjective,
	DamageDodged,
	PlayerJarated,
	PlayerExtinguished,
	PlayerJaratedFade,
	PlayerShieldBlocked,
	BreakModel,
	CheapBreakModel,
	BreakModel_Pumpkin,
	BreakModelRocketDud,
	CallVoteFailed,
	VoteStart,
	VotePass,
	VoteFailed,
	VoteSetup,
	PlayerBonusPoints,
	RDTeamPointsChanged,
	SpawnFlyingBird,
	PlayerGodRayEffect,
	PlayerTeleportHomeEffect,
	MVMStatsReset,
	MVMPlayerEvent,
	MVMResetPlayerStats,
	MVMWaveFailed,
	MVMAnnouncement,
	MVMPlayerUpgradedEvent,
	MVMVictory,
	MVMWaveChange,
	MVMLocalPlayerUpgradesClear,
	MVMLocalPlayerUpgradesValue,
	MVMResetPlayerWaveSpendingStats,
	MVMLocalPlayerWaveSpendingValue,
	MVMResetPlayerUpgradeSpending,
	MVMServerKickTimeUpdate,
	PlayerLoadoutUpdated,
	PlayerTauntSoundLoopStart,
	PlayerTauntSoundLoopEnd,
	ForcePlayerViewAngles,
	BonusDucks,
	EOTLDuckEvent,
	PlayerPickupWeapon,
	QuestObjectiveCompleted,
	SPHapWeapEvent,
	HapDmg,
	HapPunch,
	HapSetDrag,
	HapSetConst,
	HapMeleeContact
};

enum
{
	SERVER_MODIFICATION_ITEM_DURATION_IN_MINUTES = 120
};

enum CastVote
{
	VOTE_OPTION1,
	VOTE_OPTION2,
	VOTE_OPTION3,
	VOTE_OPTION4,
	VOTE_OPTION5,
	VOTE_UNCAST
};

enum
{
	ENTITY_DISSOLVE_NORMAL = 0,
	ENTITY_DISSOLVE_ELECTRICAL,
	ENTITY_DISSOLVE_ELECTRICAL_LIGHT,
	ENTITY_DISSOLVE_CORE,
	ENTITY_DISSOLVE_BITS = 3
};

enum EObserverModes
{
	OBS_MODE_NONE = 0,		// not in spectator mode
	OBS_MODE_DEATHCAM,		// special mode for death cam animation
	OBS_MODE_FREEZECAM,		// zooms to a target, and freeze-frames on them
	OBS_MODE_FIXED,			// view from a fixed camera position
	OBS_MODE_FIRSTPERSON,	// follow a player in first person view
	OBS_MODE_THIRDPERSON,	// follow a player in third person view
	OBS_MODE_ROAMING,		// free roaming
};

enum
{
	OBS_ALLOW_ALL = 0,
	OBS_ALLOW_TEAM,
	OBS_ALLOW_NONE,
	OBS_ALLOW_NUM_MODES
};

enum
{
	TYPE_TEXT = 0,
	TYPE_INDEX,
	TYPE_URL,
	TYPE_FILE
};

enum
{
	EFL_KILLME = (1 << 0),
	EFL_DORMANT = (1 << 1),
	EFL_NOCLIP_ACTIVE = (1 << 2),
	EFL_SETTING_UP_BONES = (1 << 3),
	EFL_KEEP_ON_RECREATE_ENTITIES = (1 << 4),
	EFL_HAS_PLAYER_CHILD = (1 << 4),
	EFL_DIRTY_SHADOWUPDATE = (1 << 5),
	EFL_NOTIFY = (1 << 6),
	EFL_FORCE_CHECK_TRANSMIT = (1 << 7),
	EFL_BOT_FROZEN = (1 << 8),
	EFL_SERVER_ONLY = (1 << 9),
	EFL_NO_AUTO_EDICT_ATTACH = (1 << 10),
	EFL_DIRTY_ABSTRANSFORM = (1 << 11),
	EFL_DIRTY_ABSVELOCITY = (1 << 12),
	EFL_DIRTY_ABSANGVELOCITY = (1 << 13),
	EFL_DIRTY_SURROUNDING_COLLISION_BOUNDS = (1 << 14),
	EFL_DIRTY_SPATIAL_PARTITION = (1 << 15),
	EFL_IN_SKYBOX = (1 << 17),
	EFL_USE_PARTITION_WHEN_NOT_SOLID = (1 << 18),
	EFL_TOUCHING_FLUID = (1 << 19),
	EFL_IS_BEING_LIFTED_BY_BARNACLE = (1 << 20),
	EFL_NO_ROTORWASH_PUSH = (1 << 21),
	EFL_NO_THINK_FUNCTION = (1 << 22),
	EFL_NO_GAME_PHYSICS_SIMULATION = (1 << 23),
	EFL_CHECK_UNTOUCH = (1 << 24),
	EFL_DONTBLOCKLOS = (1 << 25),
	EFL_DONTWALKON = (1 << 26),
	EFL_NO_DISSOLVE = (1 << 27),
	EFL_NO_MEGAPHYSCANNON_RAGDOLL = (1 << 28),
	EFL_NO_WATER_VELOCITY_CHANGE = (1 << 29),
	EFL_NO_PHYSCANNON_INTERACTION = (1 << 30),
	EFL_NO_DAMAGE_FORCES = (1 << 31)
};

class CBaseEntity;

struct FireBulletsInfo_t
{
	int m_iShots;
	Vector m_vecSrc;
	Vector m_vecDirShooting;
	Vector m_vecSpread;
	float m_flDistance;
	int m_iAmmoType;
	int m_iTracerFreq;
	float m_flDamage;
	int m_iPlayerDamage;
	int m_nFlags;
	float m_flDamageForceScale;
	CBaseEntity* m_pAttacker;
	CBaseEntity* m_pAdditionalIgnoreEnt;
	bool m_bPrimaryAttack;
	bool m_bUseServerRandomSeed;
};

enum
{
	VISION_MODE_NONE = 0,
	VISION_MODE_PYRO,
	VISION_MODE_HALLOWEEN,
	VISION_MODE_ROME,
	MAX_VISION_MODES
};

class CHudTexture
{
public:
	void* vftp;
	char szShortName[64];
	char szTextureFile[64];
	bool bRenderUsingFont;
	bool bPrecached;
	char cCharacterInFont;
	unsigned long hFont;
	int textureId;
	float texCoords[4];
	wrect_t rc;

public:
	int Width() const
	{
		return rc.right - rc.left;
	}
	int Height() const
	{
		return rc.bottom - rc.top;
	}
};
