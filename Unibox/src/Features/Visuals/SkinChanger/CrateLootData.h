#pragma once
#include <cstdint>
#include <span>

namespace CrateLootData
{
// Retired client-hidden lists, verified against the historical crate contents.
// https://www.trade.tf/crates (content membership only; not official GC odds).
	inline constexpr uint16_t kSeries6[] = { 339, 316, 315, 337, 338, 342, 319, 324, 341, 314, 340, 322, 330, 321, 323, 313, 329, 317, 331, 326, 327 };
	inline constexpr uint16_t kSeries22[] = { 483, 479, 485, 478, 480, 484, 481, 482, 457 };
	inline constexpr uint16_t kSeries35[] = { 658, 669, 663, 664, 654, 662, 665, 661, 659, 660 };
	inline constexpr uint16_t kSeries36[] = { 647, 650, 645, 644, 671, 652, 641, 643, 670, 653, 651, 648, 656, 649 };
	inline constexpr uint16_t kSeries46[] = { 741, 776, 780, 738, 777, 782, 781, 783, 779, 778, 784, 731, 732, 745, 739 };
	inline constexpr uint16_t kSeries48[] = { 733, 830, 829, 825, 827, 826, 821, 856, 824, 823 };
	inline constexpr uint16_t kSeries51[] = { 920, 925, 930, 939, 937, 931, 917, 921, 932, 922, 938, 869, 936, 923, 919, 924, 926, 927, 929, 918, 935, 934 };
	inline constexpr uint16_t kSeries52[] = { 1005, 1006, 1002, 1007, 1003, 999, 1000, 1004, 1001 };
	inline constexpr uint16_t kSeries53[] = { 987, 976, 977, 983, 978, 979, 980, 981, 982, 986, 984, 985, 988 };
	inline constexpr uint16_t kSeries58[] = { 30052, 30036, 30046, 30027, 30015, 30003, 30048, 30032, 30054, 30044, 30043, 30011, 30051, 30053, 30002, 30008, 30014, 30034, 30055, 30042, 30050, 30009, 30033, 30017, 30038, 30028, 30010, 30056, 30023, 30035, 30045, 30030, 30022, 30047, 30018, 30020, 30037, 30049, 30031, 30001, 30006, 30019, 30026, 30057, 30040, 30039, 30021, 30013, 30012, 30005, 30004, 30007, 30025, 30029, 30024, 30016, 30041 };
	inline constexpr uint16_t kSeries62[] = { 30132, 30140, 30099, 30080, 30119, 30090, 30112, 30127 };
	inline constexpr uint16_t kSeries63[] = { 30126, 30108, 30128, 30087, 30135, 30107, 30114, 30096 };
	inline constexpr uint16_t kSeries64[] = { 30129, 30134, 30082, 30116, 30095, 30125, 30139, 30094 };
	inline constexpr uint16_t kSeries65[] = { 30104, 30089, 30097, 30133, 30101, 30138, 30106, 30120 };
	inline constexpr uint16_t kSeries66[] = { 30085, 30100, 30084, 30098, 30124, 30123, 30093, 30081 };
	inline constexpr uint16_t kSeries67[] = { 30077, 30079, 30121, 30078, 30110, 30091, 30103, 30117 };
	inline constexpr uint16_t kSeries68[] = { 30076, 30115, 30131, 30105, 30137, 30130, 30113, 30122 };
	inline constexpr uint16_t kSeries69[] = { 30141, 30109, 30142, 30086, 30092, 30083, 30118, 30136 };
	inline constexpr uint16_t kSeries74[] = { 30237, 30297, 30243, 30192, 30239, 30238, 30284, 30282, 30197, 30211, 30279, 30203, 30194, 30195, 30278, 30215, 30225, 30193, 30231, 30259, 30286, 30289, 30191, 30283, 30240, 30269, 30205, 30222, 30252, 30281, 30288, 30227, 30217, 30223, 30274, 30290, 30207, 30213, 30253, 30260, 30221, 30226, 30266, 30276, 30196, 30200, 30220, 30232, 30198, 30204, 30264, 30267, 30273, 30293, 30298, 30302, 30303, 30206, 30208, 30212, 30218, 30229, 30230, 30233, 30234, 30241, 30248, 30249, 30254, 30255, 30261, 30270, 30275, 30280, 30287, 30294, 30299, 30219, 30228, 30235, 30258, 30263, 30285, 30295, 30199, 30242, 30251, 30214, 30216, 30236, 30245, 30256, 30257, 30265, 30277, 30292, 30296, 30301, 30247, 30268, 30224 };
	inline constexpr uint16_t kSeries78[] = { 1085, 1079, 1082, 1081, 1080, 1086, 1078, 1083, 1084 };
	inline constexpr uint16_t kSeries79[] = { 30305, 30331, 30330, 30310, 30311, 30309, 30312, 30317, 30323, 30322, 30325, 30304, 30324, 30328, 30319, 30320, 30326, 30334, 30335, 30306, 30308, 30314, 30329, 30313, 30316, 30307, 30332, 30315, 30318, 30321, 30327, 30333 };
	inline constexpr uint16_t kSeries81[] = { 30375, 30356, 30367, 30377, 30347, 30348, 30342, 30373, 30353, 30362, 30372, 30339, 30338, 30363, 30337, 30349, 30361, 30341, 30366, 30345, 30378, 30343, 30344, 30365, 30359, 30371, 30358, 30376, 30340, 30368, 30369, 30351, 30379, 30355, 30336, 30374, 30350, 30357, 30364, 30346, 30354, 30352, 30360 };
	inline constexpr uint16_t kSeries87[] = { 30570, 30550, 30539, 30544, 30552, 30554, 30557, 30558, 30538, 30542, 30541, 30548, 30555, 30556, 30551, 30540, 30546, 30547, 30549, 30543, 30545, 30553, 1127 };
	inline constexpr uint16_t kSeries88[] = { 1141, 1146, 1142, 1149, 1144, 1145, 1143 };
	inline constexpr uint16_t kSeries89[] = { 30599, 30602, 30580, 30574, 30581, 30590, 30605, 30597, 30578, 30584, 30583, 30592, 30576, 30598, 30589, 30603, 30604, 30595, 30582, 30588, 30587, 30573, 30596, 30606, 30591, 30575, 30586, 30593, 30600 };
	inline constexpr uint16_t kStockpile[] = { 30397, 30424, 30407, 30412, 30388, 30400, 30413, 30408, 30394, 30420, 30389, 30423, 30411, 30390, 30396, 30395, 30419, 30422, 30414, 30425, 30429, 30391, 30392, 30405, 30430, 30421, 30431, 30393, 30399, 30401, 30402, 30404, 30410, 30428, 30398, 30403, 30406, 30415, 30416, 30426, 30427, 30417, 30409, 30418 };

	inline std::span<const uint16_t> RetiredContents(int iSeries)
	{
		switch (iSeries)
		{
		case 6: return kSeries6;
		case 22: return kSeries22;
		case 35: return kSeries35;
		case 36: return kSeries36;
		case 46: return kSeries46;
		case 48: return kSeries48;
		case 51: return kSeries51;
		case 52: return kSeries52;
		case 53: return kSeries53;
		case 58: return kSeries58;
		case 62: return kSeries62;
		case 63: return kSeries63;
		case 64: return kSeries64;
		case 65: return kSeries65;
		case 66: return kSeries66;
		case 67: return kSeries67;
		case 68: return kSeries68;
		case 69: return kSeries69;
		case 74: return kSeries74;
		case 78: return kSeries78;
		case 79: return kSeries79;
		case 81: return kSeries81;
		case 87: return kSeries87;
		case 88: return kSeries88;
		case 89: return kSeries89;
		default: return {};
		}
	}

	inline std::span<const uint16_t> StockpileContents() { return kStockpile; }

	inline uint16_t RetiredKey(uint16_t iDef)
	{
		switch (iDef)
		{
		case 5048: return 5049;
		case 5066: return 5067;
		case 5070: return 5072;
		case 5071: return 5073;
		case 5078: return 5079;
		case 5080: return 5081;
		case 5627: return 5628;
		case 5629: return 5631;
		case 5630: return 5632;
		case 5712: return 5713;
		case 5714: return 5716;
		case 5715: return 5717;
		case 5761: return 5762;
		case 5789: return 5791;
		case 5790: return 5792;
		default: return 0;
		}
	}

	inline int DefaultSeries(uint16_t iDef)
	{
		switch (iDef)
		{
		case 5022: return 1;
		case 5041: return 7;
		case 5045: return 19;
		case 5048: return 6;
		case 5066: return 22;
		case 5068: return 30;
		case 5070: return 35;
		case 5071: return 36;
		case 5078: return 46;
		case 5080: return 48;
		case 5627: return 51;
		case 5629: return 52;
		case 5630: return 53;
		case 5635: return 58;
		case 5640: return 61;
		case 5642: return 62;
		case 5644: return 63;
		case 5646: return 64;
		case 5648: return 65;
		case 5650: return 66;
		case 5652: return 67;
		case 5654: return 68;
		case 5656: return 69;
		case 5660: return 60;
		case 5708: return 72;
		case 5709: return 73;
		case 5712: return 74;
		case 5714: return 78;
		case 5715: return 79;
		case 5719: return 81;
		case 5734: return 82;
		case 5735: return 83;
		case 5742: return 84;
		case 5752: return 85;
		case 5761: return 86;
		case 5774: return 87;
		case 5789: return 88;
		case 5790: return 89;
		default: return 0;
		}
	}
}
