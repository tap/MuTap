/* ------------------------------------------------------------
name: "icc_howl_detect"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn icc_howl_detect_f32 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 1
------------------------------------------------------------ */

#ifndef  __icc_howl_detect_f32_H__
#define  __icc_howl_detect_f32_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>
#ifndef FAUST_INT_WRAP
#define FAUST_INT_WRAP
inline int faust_wrap_add(int a, int b) { return int((unsigned int)a + (unsigned int)b); }
inline int faust_wrap_sub(int a, int b) { return int((unsigned int)a - (unsigned int)b); }
inline int faust_wrap_mul(int a, int b) { return int((unsigned int)a * (unsigned int)b); }
#endif


namespace mutap_faust {

#ifndef FAUSTCLASS 
#define FAUSTCLASS icc_howl_detect_f32
#endif

#ifdef __APPLE__ 
#define exp10f __exp10f
#define exp10 __exp10
#endif

#if defined(_WIN32)
#define RESTRICT __restrict
#else
#define RESTRICT __restrict__
#endif

static float icc_howl_detect_f32_faustpower2_f(float value) {
	return value * value;
}

class icc_howl_detect_f32 : public dsp {
	
 private:
	
	int fSampleRate;
	float fConst0;
	float fConst1;
	float fConst2;
	float fConst3;
	float fConst4;
	float fConst5;
	float fConst6;
	float fConst7;
	float fRec2[3];
	float fConst8;
	float fConst9;
	float fRec1[2];
	float fConst10;
	float fConst11;
	float fConst12;
	float fConst13;
	float fConst14;
	float fConst15;
	float fConst16;
	float fRec4[3];
	float fRec3[2];
	float fConst17;
	float fConst18;
	float fConst19;
	float fConst20;
	float fConst21;
	float fConst22;
	float fConst23;
	float fRec6[3];
	float fRec5[2];
	float fConst24;
	float fConst25;
	float fConst26;
	float fConst27;
	float fConst28;
	float fConst29;
	float fConst30;
	float fRec8[3];
	float fRec7[2];
	float fConst31;
	float fConst32;
	float fConst33;
	float fConst34;
	float fConst35;
	float fConst36;
	float fConst37;
	float fRec10[3];
	float fRec9[2];
	float fConst38;
	float fConst39;
	float fConst40;
	float fConst41;
	float fConst42;
	float fConst43;
	float fConst44;
	float fRec12[3];
	float fRec11[2];
	float fConst45;
	float fConst46;
	float fConst47;
	float fConst48;
	float fConst49;
	float fConst50;
	float fConst51;
	float fRec14[3];
	float fRec13[2];
	float fConst52;
	float fConst53;
	float fConst54;
	float fConst55;
	float fConst56;
	float fConst57;
	float fConst58;
	float fRec16[3];
	float fRec15[2];
	float fConst59;
	float fConst60;
	float fConst61;
	float fConst62;
	float fConst63;
	float fConst64;
	float fConst65;
	float fRec18[3];
	float fRec17[2];
	float fConst66;
	float fConst67;
	float fConst68;
	float fConst69;
	float fConst70;
	float fConst71;
	float fConst72;
	float fRec20[3];
	float fRec19[2];
	float fConst73;
	float fConst74;
	float fConst75;
	float fConst76;
	float fConst77;
	float fConst78;
	float fConst79;
	float fRec22[3];
	float fRec21[2];
	float fConst80;
	float fConst81;
	float fConst82;
	float fConst83;
	float fConst84;
	float fConst85;
	float fConst86;
	float fRec24[3];
	float fRec23[2];
	float fConst87;
	float fConst88;
	float fConst89;
	float fConst90;
	float fConst91;
	float fConst92;
	float fConst93;
	float fRec26[3];
	float fRec25[2];
	float fConst94;
	float fConst95;
	float fConst96;
	float fConst97;
	float fConst98;
	float fConst99;
	float fConst100;
	float fRec28[3];
	float fRec27[2];
	float fConst101;
	float fConst102;
	float fConst103;
	float fConst104;
	float fConst105;
	float fConst106;
	float fConst107;
	float fRec30[3];
	float fRec29[2];
	float fConst108;
	float fConst109;
	float fConst110;
	float fConst111;
	float fConst112;
	float fConst113;
	float fConst114;
	float fRec32[3];
	float fRec31[2];
	float fConst115;
	float fConst116;
	float fConst117;
	float fConst118;
	float fConst119;
	float fConst120;
	float fConst121;
	float fRec34[3];
	float fRec33[2];
	float fConst122;
	float fConst123;
	float fConst124;
	float fConst125;
	float fConst126;
	float fConst127;
	float fConst128;
	float fRec36[3];
	float fRec35[2];
	float fConst129;
	float fConst130;
	float fConst131;
	float fConst132;
	float fConst133;
	float fConst134;
	float fConst135;
	float fRec38[3];
	float fRec37[2];
	float fConst136;
	float fConst137;
	float fConst138;
	float fConst139;
	float fConst140;
	float fConst141;
	float fConst142;
	float fRec40[3];
	float fRec39[2];
	float fConst143;
	float fConst144;
	float fConst145;
	float fConst146;
	float fConst147;
	float fConst148;
	float fConst149;
	float fRec42[3];
	float fRec41[2];
	float fConst150;
	float fConst151;
	float fConst152;
	float fConst153;
	float fConst154;
	float fConst155;
	float fConst156;
	float fRec44[3];
	float fRec43[2];
	float fConst157;
	float fConst158;
	float fConst159;
	float fConst160;
	float fConst161;
	float fConst162;
	float fConst163;
	float fRec46[3];
	float fRec45[2];
	float fConst164;
	float fConst165;
	float fConst166;
	float fConst167;
	float fConst168;
	float fConst169;
	float fConst170;
	float fRec48[3];
	float fRec47[2];
	float fConst171;
	float fConst172;
	float fConst173;
	float fConst174;
	float fConst175;
	float fConst176;
	float fConst177;
	float fRec50[3];
	float fRec49[2];
	float fConst178;
	float fConst179;
	float fConst180;
	float fConst181;
	float fConst182;
	float fConst183;
	float fConst184;
	float fRec52[3];
	float fRec51[2];
	float fConst185;
	float fConst186;
	float fConst187;
	float fConst188;
	float fConst189;
	float fConst190;
	float fConst191;
	float fRec54[3];
	float fRec53[2];
	float fConst192;
	float fConst193;
	float fConst194;
	float fConst195;
	float fConst196;
	float fConst197;
	float fConst198;
	float fRec56[3];
	float fRec55[2];
	float fConst199;
	float fConst200;
	float fConst201;
	float fConst202;
	float fConst203;
	float fConst204;
	float fConst205;
	float fRec58[3];
	float fRec57[2];
	float fConst206;
	float fConst207;
	float fConst208;
	float fConst209;
	float fConst210;
	float fConst211;
	float fConst212;
	float fRec60[3];
	float fRec59[2];
	float fConst213;
	float fConst214;
	float fConst215;
	float fConst216;
	float fConst217;
	float fConst218;
	float fConst219;
	float fRec62[3];
	float fRec61[2];
	float fConst220;
	float fConst221;
	float fConst222;
	float fConst223;
	float fConst224;
	float fConst225;
	float fConst226;
	float fRec64[3];
	float fRec63[2];
	float fConst227;
	float fConst228;
	float fRec0[2];
	
 public:
	icc_howl_detect_f32() {
	}
	
	icc_howl_detect_f32(const icc_howl_detect_f32&) = default;
	
	virtual ~icc_howl_detect_f32() = default;
	
	icc_howl_detect_f32& operator=(const icc_howl_detect_f32&) = default;
	
	void metadata(Meta* m) { 
		m->declare("analyzers.lib/amp_follower_ar:author", "Jonatan Liljedahl, revised by Romain Michon");
		m->declare("analyzers.lib/name", "Faust Analyzer Library");
		m->declare("analyzers.lib/version", "1.4.0");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn icc_howl_detect_f32 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 1");
		m->declare("filename", "icc_howl_detect.dsp");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/resonbp:author", "Julius O. Smith III");
		m->declare("filters.lib/resonbp:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/resonbp:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf2:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/tf2s:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2s:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2s:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/version", "1.9.0");
		m->declare("icc.lib/author", "Ashmita Chakraborty");
		m->declare("icc.lib/license", "MIT");
		m->declare("icc.lib/name", "ICC");
		m->declare("icc.lib/version", "0.1.0");
		m->declare("icc_48k.lib/name", "ICC (48 kHz retuning)");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "icc_howl_detect");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("signals.lib/name", "Faust Routing Library");
		m->declare("signals.lib/onePoleSwitching:author", "Jonatan Liljedahl, revised by Dario Sanfilippo");
		m->declare("signals.lib/onePoleSwitching:licence", "LicenseRef-STK-4.3");
		m->declare("signals.lib/version", "1.7.0");
	}

	virtual int getNumInputs() {
		return 1;
	}
	virtual int getNumOutputs() {
		return 3;
	}
	
	static void classInit(int sample_rate) {
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = std::tan(18849.557f / fConst0);
		fConst2 = 1.0f / fConst1;
		fConst3 = (fConst2 + 0.071428575f) / fConst1 + 1.0f;
		fConst4 = 1.0f / (fConst1 * fConst3);
		fConst5 = 1.0f / fConst3;
		fConst6 = (fConst2 + -0.071428575f) / fConst1 + 1.0f;
		fConst7 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst1));
		fConst8 = std::exp(-(2e+01f / fConst0));
		fConst9 = std::exp(-(2e+02f / fConst0));
		fConst10 = std::tan(16734.848f / fConst0);
		fConst11 = 1.0f / fConst10;
		fConst12 = (fConst11 + 0.071428575f) / fConst10 + 1.0f;
		fConst13 = 1.0f / (fConst10 * fConst12);
		fConst14 = 1.0f / fConst12;
		fConst15 = (fConst11 + -0.071428575f) / fConst10 + 1.0f;
		fConst16 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst10));
		fConst17 = std::tan(14857.387f / fConst0);
		fConst18 = 1.0f / fConst17;
		fConst19 = (fConst18 + 0.071428575f) / fConst17 + 1.0f;
		fConst20 = 1.0f / (fConst17 * fConst19);
		fConst21 = 1.0f / fConst19;
		fConst22 = (fConst18 + -0.071428575f) / fConst17 + 1.0f;
		fConst23 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst17));
		fConst24 = std::tan(13190.555f / fConst0);
		fConst25 = 1.0f / fConst24;
		fConst26 = (fConst25 + 0.071428575f) / fConst24 + 1.0f;
		fConst27 = 1.0f / (fConst24 * fConst26);
		fConst28 = 1.0f / fConst26;
		fConst29 = (fConst25 + -0.071428575f) / fConst24 + 1.0f;
		fConst30 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst24));
		fConst31 = std::tan(11710.723f / fConst0);
		fConst32 = 1.0f / fConst31;
		fConst33 = (fConst32 + 0.071428575f) / fConst31 + 1.0f;
		fConst34 = 1.0f / (fConst31 * fConst33);
		fConst35 = 1.0f / fConst33;
		fConst36 = (fConst32 + -0.071428575f) / fConst31 + 1.0f;
		fConst37 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst31));
		fConst38 = std::tan(10396.911f / fConst0);
		fConst39 = 1.0f / fConst38;
		fConst40 = (fConst39 + 0.071428575f) / fConst38 + 1.0f;
		fConst41 = 1.0f / (fConst38 * fConst40);
		fConst42 = 1.0f / fConst40;
		fConst43 = (fConst39 + -0.071428575f) / fConst38 + 1.0f;
		fConst44 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst38));
		fConst45 = std::tan(9230.495f / fConst0);
		fConst46 = 1.0f / fConst45;
		fConst47 = (fConst46 + 0.071428575f) / fConst45 + 1.0f;
		fConst48 = 1.0f / (fConst45 * fConst47);
		fConst49 = 1.0f / fConst47;
		fConst50 = (fConst46 + -0.071428575f) / fConst45 + 1.0f;
		fConst51 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst45));
		fConst52 = std::tan(8194.9375f / fConst0);
		fConst53 = 1.0f / fConst52;
		fConst54 = (fConst53 + 0.071428575f) / fConst52 + 1.0f;
		fConst55 = 1.0f / (fConst52 * fConst54);
		fConst56 = 1.0f / fConst54;
		fConst57 = (fConst53 + -0.071428575f) / fConst52 + 1.0f;
		fConst58 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst52));
		fConst59 = std::tan(7275.5576f / fConst0);
		fConst60 = 1.0f / fConst59;
		fConst61 = (fConst60 + 0.071428575f) / fConst59 + 1.0f;
		fConst62 = 1.0f / (fConst59 * fConst61);
		fConst63 = 1.0f / fConst61;
		fConst64 = (fConst60 + -0.071428575f) / fConst59 + 1.0f;
		fConst65 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst59));
		fConst66 = std::tan(6459.322f / fConst0);
		fConst67 = 1.0f / fConst66;
		fConst68 = (fConst67 + 0.071428575f) / fConst66 + 1.0f;
		fConst69 = 1.0f / (fConst66 * fConst68);
		fConst70 = 1.0f / fConst68;
		fConst71 = (fConst67 + -0.071428575f) / fConst66 + 1.0f;
		fConst72 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst66));
		fConst73 = std::tan(5734.6587f / fConst0);
		fConst74 = 1.0f / fConst73;
		fConst75 = (fConst74 + 0.071428575f) / fConst73 + 1.0f;
		fConst76 = 1.0f / (fConst73 * fConst75);
		fConst77 = 1.0f / fConst75;
		fConst78 = (fConst74 + -0.071428575f) / fConst73 + 1.0f;
		fConst79 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst73));
		fConst80 = std::tan(5091.2944f / fConst0);
		fConst81 = 1.0f / fConst80;
		fConst82 = (fConst81 + 0.071428575f) / fConst80 + 1.0f;
		fConst83 = 1.0f / (fConst80 * fConst82);
		fConst84 = 1.0f / fConst82;
		fConst85 = (fConst81 + -0.071428575f) / fConst80 + 1.0f;
		fConst86 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst80));
		fConst87 = std::tan(4520.1084f / fConst0);
		fConst88 = 1.0f / fConst87;
		fConst89 = (fConst88 + 0.071428575f) / fConst87 + 1.0f;
		fConst90 = 1.0f / (fConst87 * fConst89);
		fConst91 = 1.0f / fConst89;
		fConst92 = (fConst88 + -0.071428575f) / fConst87 + 1.0f;
		fConst93 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst87));
		fConst94 = std::tan(4013.0032f / fConst0);
		fConst95 = 1.0f / fConst94;
		fConst96 = (fConst95 + 0.071428575f) / fConst94 + 1.0f;
		fConst97 = 1.0f / (fConst94 * fConst96);
		fConst98 = 1.0f / fConst96;
		fConst99 = (fConst95 + -0.071428575f) / fConst94 + 1.0f;
		fConst100 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst94));
		fConst101 = std::tan(3562.7896f / fConst0);
		fConst102 = 1.0f / fConst101;
		fConst103 = (fConst102 + 0.071428575f) / fConst101 + 1.0f;
		fConst104 = 1.0f / (fConst101 * fConst103);
		fConst105 = 1.0f / fConst103;
		fConst106 = (fConst102 + -0.071428575f) / fConst101 + 1.0f;
		fConst107 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst101));
		fConst108 = std::tan(3163.0845f / fConst0);
		fConst109 = 1.0f / fConst108;
		fConst110 = (fConst109 + 0.071428575f) / fConst108 + 1.0f;
		fConst111 = 1.0f / (fConst108 * fConst110);
		fConst112 = 1.0f / fConst110;
		fConst113 = (fConst109 + -0.071428575f) / fConst108 + 1.0f;
		fConst114 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst108));
		fConst115 = std::tan(2808.2222f / fConst0);
		fConst116 = 1.0f / fConst115;
		fConst117 = (fConst116 + 0.071428575f) / fConst115 + 1.0f;
		fConst118 = 1.0f / (fConst115 * fConst117);
		fConst119 = 1.0f / fConst117;
		fConst120 = (fConst116 + -0.071428575f) / fConst115 + 1.0f;
		fConst121 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst115));
		fConst122 = std::tan(2493.1711f / fConst0);
		fConst123 = 1.0f / fConst122;
		fConst124 = (fConst123 + 0.071428575f) / fConst122 + 1.0f;
		fConst125 = 1.0f / (fConst122 * fConst124);
		fConst126 = 1.0f / fConst124;
		fConst127 = (fConst123 + -0.071428575f) / fConst122 + 1.0f;
		fConst128 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst122));
		fConst129 = std::tan(2213.4653f / fConst0);
		fConst130 = 1.0f / fConst129;
		fConst131 = (fConst130 + 0.071428575f) / fConst129 + 1.0f;
		fConst132 = 1.0f / (fConst129 * fConst131);
		fConst133 = 1.0f / fConst131;
		fConst134 = (fConst130 + -0.071428575f) / fConst129 + 1.0f;
		fConst135 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst129));
		fConst136 = std::tan(1965.1395f / fConst0);
		fConst137 = 1.0f / fConst136;
		fConst138 = (fConst137 + 0.071428575f) / fConst136 + 1.0f;
		fConst139 = 1.0f / (fConst136 * fConst138);
		fConst140 = 1.0f / fConst138;
		fConst141 = (fConst137 + -0.071428575f) / fConst136 + 1.0f;
		fConst142 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst136));
		fConst143 = std::tan(1744.673f / fConst0);
		fConst144 = 1.0f / fConst143;
		fConst145 = (fConst144 + 0.071428575f) / fConst143 + 1.0f;
		fConst146 = 1.0f / (fConst143 * fConst145);
		fConst147 = 1.0f / fConst145;
		fConst148 = (fConst144 + -0.071428575f) / fConst143 + 1.0f;
		fConst149 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst143));
		fConst150 = std::tan(1548.9403f / fConst0);
		fConst151 = 1.0f / fConst150;
		fConst152 = (fConst151 + 0.071428575f) / fConst150 + 1.0f;
		fConst153 = 1.0f / (fConst150 * fConst152);
		fConst154 = 1.0f / fConst152;
		fConst155 = (fConst151 + -0.071428575f) / fConst150 + 1.0f;
		fConst156 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst150));
		fConst157 = std::tan(1375.1666f / fConst0);
		fConst158 = 1.0f / fConst157;
		fConst159 = (fConst158 + 0.071428575f) / fConst157 + 1.0f;
		fConst160 = 1.0f / (fConst157 * fConst159);
		fConst161 = 1.0f / fConst159;
		fConst162 = (fConst158 + -0.071428575f) / fConst157 + 1.0f;
		fConst163 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst157));
		fConst164 = std::tan(1220.8884f / fConst0);
		fConst165 = 1.0f / fConst164;
		fConst166 = (fConst165 + 0.071428575f) / fConst164 + 1.0f;
		fConst167 = 1.0f / (fConst164 * fConst166);
		fConst168 = 1.0f / fConst166;
		fConst169 = (fConst165 + -0.071428575f) / fConst164 + 1.0f;
		fConst170 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst164));
		fConst171 = std::tan(1083.9185f / fConst0);
		fConst172 = 1.0f / fConst171;
		fConst173 = (fConst172 + 0.071428575f) / fConst171 + 1.0f;
		fConst174 = 1.0f / (fConst171 * fConst173);
		fConst175 = 1.0f / fConst173;
		fConst176 = (fConst172 + -0.071428575f) / fConst171 + 1.0f;
		fConst177 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst171));
		fConst178 = std::tan(962.315f / fConst0);
		fConst179 = 1.0f / fConst178;
		fConst180 = (fConst179 + 0.071428575f) / fConst178 + 1.0f;
		fConst181 = 1.0f / (fConst178 * fConst180);
		fConst182 = 1.0f / fConst180;
		fConst183 = (fConst179 + -0.071428575f) / fConst178 + 1.0f;
		fConst184 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst178));
		fConst185 = std::tan(854.35406f / fConst0);
		fConst186 = 1.0f / fConst185;
		fConst187 = (fConst186 + 0.071428575f) / fConst185 + 1.0f;
		fConst188 = 1.0f / (fConst185 * fConst187);
		fConst189 = 1.0f / fConst187;
		fConst190 = (fConst186 + -0.071428575f) / fConst185 + 1.0f;
		fConst191 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst185));
		fConst192 = std::tan(758.5052f / fConst0);
		fConst193 = 1.0f / fConst192;
		fConst194 = (fConst193 + 0.071428575f) / fConst192 + 1.0f;
		fConst195 = 1.0f / (fConst192 * fConst194);
		fConst196 = 1.0f / fConst194;
		fConst197 = (fConst193 + -0.071428575f) / fConst192 + 1.0f;
		fConst198 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst192));
		fConst199 = std::tan(673.4094f / fConst0);
		fConst200 = 1.0f / fConst199;
		fConst201 = (fConst200 + 0.071428575f) / fConst199 + 1.0f;
		fConst202 = 1.0f / (fConst199 * fConst201);
		fConst203 = 1.0f / fConst201;
		fConst204 = (fConst200 + -0.071428575f) / fConst199 + 1.0f;
		fConst205 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst199));
		fConst206 = std::tan(597.8605f / fConst0);
		fConst207 = 1.0f / fConst206;
		fConst208 = (fConst207 + 0.071428575f) / fConst206 + 1.0f;
		fConst209 = 1.0f / (fConst206 * fConst208);
		fConst210 = 1.0f / fConst208;
		fConst211 = (fConst207 + -0.071428575f) / fConst206 + 1.0f;
		fConst212 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst206));
		fConst213 = std::tan(530.78723f / fConst0);
		fConst214 = 1.0f / fConst213;
		fConst215 = (fConst214 + 0.071428575f) / fConst213 + 1.0f;
		fConst216 = 1.0f / (fConst213 * fConst215);
		fConst217 = 1.0f / fConst215;
		fConst218 = (fConst214 + -0.071428575f) / fConst213 + 1.0f;
		fConst219 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst213));
		fConst220 = std::tan(471.2389f / fConst0);
		fConst221 = 1.0f / fConst220;
		fConst222 = (fConst221 + 0.071428575f) / fConst220 + 1.0f;
		fConst223 = 1.0f / (fConst220 * fConst222);
		fConst224 = 1.0f / fConst222;
		fConst225 = (fConst221 + -0.071428575f) / fConst220 + 1.0f;
		fConst226 = 2.0f * (1.0f - 1.0f / icc_howl_detect_f32_faustpower2_f(fConst220));
		fConst227 = std::exp(-(1.25f / fConst0));
		fConst228 = std::exp(-(5.0f / fConst0));
	}
	
	virtual void instanceResetUserInterface() {
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 3; l0 = faust_wrap_add(l0, 1)) {
			fRec2[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			fRec1[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 3; l2 = faust_wrap_add(l2, 1)) {
			fRec4[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fRec3[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 3; l4 = faust_wrap_add(l4, 1)) {
			fRec6[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec5[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 3; l6 = faust_wrap_add(l6, 1)) {
			fRec8[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = faust_wrap_add(l7, 1)) {
			fRec7[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 3; l8 = faust_wrap_add(l8, 1)) {
			fRec10[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = faust_wrap_add(l9, 1)) {
			fRec9[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 3; l10 = faust_wrap_add(l10, 1)) {
			fRec12[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = faust_wrap_add(l11, 1)) {
			fRec11[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 3; l12 = faust_wrap_add(l12, 1)) {
			fRec14[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = faust_wrap_add(l13, 1)) {
			fRec13[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 3; l14 = faust_wrap_add(l14, 1)) {
			fRec16[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 2; l15 = faust_wrap_add(l15, 1)) {
			fRec15[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 3; l16 = faust_wrap_add(l16, 1)) {
			fRec18[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 2; l17 = faust_wrap_add(l17, 1)) {
			fRec17[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 3; l18 = faust_wrap_add(l18, 1)) {
			fRec20[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2; l19 = faust_wrap_add(l19, 1)) {
			fRec19[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 3; l20 = faust_wrap_add(l20, 1)) {
			fRec22[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 2; l21 = faust_wrap_add(l21, 1)) {
			fRec21[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 3; l22 = faust_wrap_add(l22, 1)) {
			fRec24[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 2; l23 = faust_wrap_add(l23, 1)) {
			fRec23[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 3; l24 = faust_wrap_add(l24, 1)) {
			fRec26[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 2; l25 = faust_wrap_add(l25, 1)) {
			fRec25[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 3; l26 = faust_wrap_add(l26, 1)) {
			fRec28[l26] = 0.0f;
		}
		for (int l27 = 0; l27 < 2; l27 = faust_wrap_add(l27, 1)) {
			fRec27[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 3; l28 = faust_wrap_add(l28, 1)) {
			fRec30[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 2; l29 = faust_wrap_add(l29, 1)) {
			fRec29[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 3; l30 = faust_wrap_add(l30, 1)) {
			fRec32[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 2; l31 = faust_wrap_add(l31, 1)) {
			fRec31[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 3; l32 = faust_wrap_add(l32, 1)) {
			fRec34[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2; l33 = faust_wrap_add(l33, 1)) {
			fRec33[l33] = 0.0f;
		}
		for (int l34 = 0; l34 < 3; l34 = faust_wrap_add(l34, 1)) {
			fRec36[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 2; l35 = faust_wrap_add(l35, 1)) {
			fRec35[l35] = 0.0f;
		}
		for (int l36 = 0; l36 < 3; l36 = faust_wrap_add(l36, 1)) {
			fRec38[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 2; l37 = faust_wrap_add(l37, 1)) {
			fRec37[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 3; l38 = faust_wrap_add(l38, 1)) {
			fRec40[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 2; l39 = faust_wrap_add(l39, 1)) {
			fRec39[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 3; l40 = faust_wrap_add(l40, 1)) {
			fRec42[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 2; l41 = faust_wrap_add(l41, 1)) {
			fRec41[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 3; l42 = faust_wrap_add(l42, 1)) {
			fRec44[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 2; l43 = faust_wrap_add(l43, 1)) {
			fRec43[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 3; l44 = faust_wrap_add(l44, 1)) {
			fRec46[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 2; l45 = faust_wrap_add(l45, 1)) {
			fRec45[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 3; l46 = faust_wrap_add(l46, 1)) {
			fRec48[l46] = 0.0f;
		}
		for (int l47 = 0; l47 < 2; l47 = faust_wrap_add(l47, 1)) {
			fRec47[l47] = 0.0f;
		}
		for (int l48 = 0; l48 < 3; l48 = faust_wrap_add(l48, 1)) {
			fRec50[l48] = 0.0f;
		}
		for (int l49 = 0; l49 < 2; l49 = faust_wrap_add(l49, 1)) {
			fRec49[l49] = 0.0f;
		}
		for (int l50 = 0; l50 < 3; l50 = faust_wrap_add(l50, 1)) {
			fRec52[l50] = 0.0f;
		}
		for (int l51 = 0; l51 < 2; l51 = faust_wrap_add(l51, 1)) {
			fRec51[l51] = 0.0f;
		}
		for (int l52 = 0; l52 < 3; l52 = faust_wrap_add(l52, 1)) {
			fRec54[l52] = 0.0f;
		}
		for (int l53 = 0; l53 < 2; l53 = faust_wrap_add(l53, 1)) {
			fRec53[l53] = 0.0f;
		}
		for (int l54 = 0; l54 < 3; l54 = faust_wrap_add(l54, 1)) {
			fRec56[l54] = 0.0f;
		}
		for (int l55 = 0; l55 < 2; l55 = faust_wrap_add(l55, 1)) {
			fRec55[l55] = 0.0f;
		}
		for (int l56 = 0; l56 < 3; l56 = faust_wrap_add(l56, 1)) {
			fRec58[l56] = 0.0f;
		}
		for (int l57 = 0; l57 < 2; l57 = faust_wrap_add(l57, 1)) {
			fRec57[l57] = 0.0f;
		}
		for (int l58 = 0; l58 < 3; l58 = faust_wrap_add(l58, 1)) {
			fRec60[l58] = 0.0f;
		}
		for (int l59 = 0; l59 < 2; l59 = faust_wrap_add(l59, 1)) {
			fRec59[l59] = 0.0f;
		}
		for (int l60 = 0; l60 < 3; l60 = faust_wrap_add(l60, 1)) {
			fRec62[l60] = 0.0f;
		}
		for (int l61 = 0; l61 < 2; l61 = faust_wrap_add(l61, 1)) {
			fRec61[l61] = 0.0f;
		}
		for (int l62 = 0; l62 < 3; l62 = faust_wrap_add(l62, 1)) {
			fRec64[l62] = 0.0f;
		}
		for (int l63 = 0; l63 < 2; l63 = faust_wrap_add(l63, 1)) {
			fRec63[l63] = 0.0f;
		}
		for (int l64 = 0; l64 < 2; l64 = faust_wrap_add(l64, 1)) {
			fRec0[l64] = 0.0f;
		}
	}
	
	virtual void init(int sample_rate) {
		classInit(sample_rate);
		instanceInit(sample_rate);
	}
	
	virtual void instanceInit(int sample_rate) {
		instanceConstants(sample_rate);
		instanceResetUserInterface();
		instanceClear();
	}
	
	virtual icc_howl_detect_f32* clone() {
		return new icc_howl_detect_f32(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("icc_howl_detect");
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		FAUSTFLOAT* output2 = outputs[2];
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			float fTemp0 = static_cast<float>(input0[i0]);
			float fTemp1 = fTemp0 - fConst5 * (fConst6 * fRec2[2] + fConst7 * fRec2[1]);
			fRec2[0] = ((std::fabs(fTemp1) > 1.1754944e-38f) ? fTemp1 : 0.0f);
			float fTemp2 = std::fabs(fConst4 * (fRec2[0] - fRec2[2]));
			float fTemp3 = ((fTemp2 > fRec1[1]) ? fConst9 : fConst8);
			float fTemp4 = fTemp2 * (1.0f - fTemp3) + fRec1[1] * fTemp3;
			fRec1[0] = ((std::fabs(fTemp4) > 1.1754944e-38f) ? fTemp4 : 0.0f);
			float fTemp5 = fTemp0 - fConst14 * (fConst15 * fRec4[2] + fConst16 * fRec4[1]);
			fRec4[0] = ((std::fabs(fTemp5) > 1.1754944e-38f) ? fTemp5 : 0.0f);
			float fTemp6 = std::fabs(fConst13 * (fRec4[0] - fRec4[2]));
			float fTemp7 = ((fTemp6 > fRec3[1]) ? fConst9 : fConst8);
			float fTemp8 = fTemp6 * (1.0f - fTemp7) + fRec3[1] * fTemp7;
			fRec3[0] = ((std::fabs(fTemp8) > 1.1754944e-38f) ? fTemp8 : 0.0f);
			float fTemp9 = fTemp0 - fConst21 * (fConst22 * fRec6[2] + fConst23 * fRec6[1]);
			fRec6[0] = ((std::fabs(fTemp9) > 1.1754944e-38f) ? fTemp9 : 0.0f);
			float fTemp10 = std::fabs(fConst20 * (fRec6[0] - fRec6[2]));
			float fTemp11 = ((fTemp10 > fRec5[1]) ? fConst9 : fConst8);
			float fTemp12 = fTemp10 * (1.0f - fTemp11) + fRec5[1] * fTemp11;
			fRec5[0] = ((std::fabs(fTemp12) > 1.1754944e-38f) ? fTemp12 : 0.0f);
			float fTemp13 = fTemp0 - fConst28 * (fConst29 * fRec8[2] + fConst30 * fRec8[1]);
			fRec8[0] = ((std::fabs(fTemp13) > 1.1754944e-38f) ? fTemp13 : 0.0f);
			float fTemp14 = std::fabs(fConst27 * (fRec8[0] - fRec8[2]));
			float fTemp15 = ((fTemp14 > fRec7[1]) ? fConst9 : fConst8);
			float fTemp16 = fTemp14 * (1.0f - fTemp15) + fRec7[1] * fTemp15;
			fRec7[0] = ((std::fabs(fTemp16) > 1.1754944e-38f) ? fTemp16 : 0.0f);
			float fTemp17 = fTemp0 - fConst35 * (fConst36 * fRec10[2] + fConst37 * fRec10[1]);
			fRec10[0] = ((std::fabs(fTemp17) > 1.1754944e-38f) ? fTemp17 : 0.0f);
			float fTemp18 = std::fabs(fConst34 * (fRec10[0] - fRec10[2]));
			float fTemp19 = ((fTemp18 > fRec9[1]) ? fConst9 : fConst8);
			float fTemp20 = fTemp18 * (1.0f - fTemp19) + fRec9[1] * fTemp19;
			fRec9[0] = ((std::fabs(fTemp20) > 1.1754944e-38f) ? fTemp20 : 0.0f);
			float fTemp21 = fTemp0 - fConst42 * (fConst43 * fRec12[2] + fConst44 * fRec12[1]);
			fRec12[0] = ((std::fabs(fTemp21) > 1.1754944e-38f) ? fTemp21 : 0.0f);
			float fTemp22 = std::fabs(fConst41 * (fRec12[0] - fRec12[2]));
			float fTemp23 = ((fTemp22 > fRec11[1]) ? fConst9 : fConst8);
			float fTemp24 = fTemp22 * (1.0f - fTemp23) + fRec11[1] * fTemp23;
			fRec11[0] = ((std::fabs(fTemp24) > 1.1754944e-38f) ? fTemp24 : 0.0f);
			float fTemp25 = fTemp0 - fConst49 * (fConst50 * fRec14[2] + fConst51 * fRec14[1]);
			fRec14[0] = ((std::fabs(fTemp25) > 1.1754944e-38f) ? fTemp25 : 0.0f);
			float fTemp26 = std::fabs(fConst48 * (fRec14[0] - fRec14[2]));
			float fTemp27 = ((fTemp26 > fRec13[1]) ? fConst9 : fConst8);
			float fTemp28 = fTemp26 * (1.0f - fTemp27) + fRec13[1] * fTemp27;
			fRec13[0] = ((std::fabs(fTemp28) > 1.1754944e-38f) ? fTemp28 : 0.0f);
			float fTemp29 = fTemp0 - fConst56 * (fConst57 * fRec16[2] + fConst58 * fRec16[1]);
			fRec16[0] = ((std::fabs(fTemp29) > 1.1754944e-38f) ? fTemp29 : 0.0f);
			float fTemp30 = std::fabs(fConst55 * (fRec16[0] - fRec16[2]));
			float fTemp31 = ((fTemp30 > fRec15[1]) ? fConst9 : fConst8);
			float fTemp32 = fTemp30 * (1.0f - fTemp31) + fRec15[1] * fTemp31;
			fRec15[0] = ((std::fabs(fTemp32) > 1.1754944e-38f) ? fTemp32 : 0.0f);
			float fTemp33 = fTemp0 - fConst63 * (fConst64 * fRec18[2] + fConst65 * fRec18[1]);
			fRec18[0] = ((std::fabs(fTemp33) > 1.1754944e-38f) ? fTemp33 : 0.0f);
			float fTemp34 = std::fabs(fConst62 * (fRec18[0] - fRec18[2]));
			float fTemp35 = ((fTemp34 > fRec17[1]) ? fConst9 : fConst8);
			float fTemp36 = fTemp34 * (1.0f - fTemp35) + fRec17[1] * fTemp35;
			fRec17[0] = ((std::fabs(fTemp36) > 1.1754944e-38f) ? fTemp36 : 0.0f);
			float fTemp37 = fTemp0 - fConst70 * (fConst71 * fRec20[2] + fConst72 * fRec20[1]);
			fRec20[0] = ((std::fabs(fTemp37) > 1.1754944e-38f) ? fTemp37 : 0.0f);
			float fTemp38 = std::fabs(fConst69 * (fRec20[0] - fRec20[2]));
			float fTemp39 = ((fTemp38 > fRec19[1]) ? fConst9 : fConst8);
			float fTemp40 = fTemp38 * (1.0f - fTemp39) + fRec19[1] * fTemp39;
			fRec19[0] = ((std::fabs(fTemp40) > 1.1754944e-38f) ? fTemp40 : 0.0f);
			float fTemp41 = fTemp0 - fConst77 * (fConst78 * fRec22[2] + fConst79 * fRec22[1]);
			fRec22[0] = ((std::fabs(fTemp41) > 1.1754944e-38f) ? fTemp41 : 0.0f);
			float fTemp42 = std::fabs(fConst76 * (fRec22[0] - fRec22[2]));
			float fTemp43 = ((fTemp42 > fRec21[1]) ? fConst9 : fConst8);
			float fTemp44 = fTemp42 * (1.0f - fTemp43) + fRec21[1] * fTemp43;
			fRec21[0] = ((std::fabs(fTemp44) > 1.1754944e-38f) ? fTemp44 : 0.0f);
			float fTemp45 = fTemp0 - fConst84 * (fConst85 * fRec24[2] + fConst86 * fRec24[1]);
			fRec24[0] = ((std::fabs(fTemp45) > 1.1754944e-38f) ? fTemp45 : 0.0f);
			float fTemp46 = std::fabs(fConst83 * (fRec24[0] - fRec24[2]));
			float fTemp47 = ((fTemp46 > fRec23[1]) ? fConst9 : fConst8);
			float fTemp48 = fTemp46 * (1.0f - fTemp47) + fRec23[1] * fTemp47;
			fRec23[0] = ((std::fabs(fTemp48) > 1.1754944e-38f) ? fTemp48 : 0.0f);
			float fTemp49 = fTemp0 - fConst91 * (fConst92 * fRec26[2] + fConst93 * fRec26[1]);
			fRec26[0] = ((std::fabs(fTemp49) > 1.1754944e-38f) ? fTemp49 : 0.0f);
			float fTemp50 = std::fabs(fConst90 * (fRec26[0] - fRec26[2]));
			float fTemp51 = ((fTemp50 > fRec25[1]) ? fConst9 : fConst8);
			float fTemp52 = fTemp50 * (1.0f - fTemp51) + fRec25[1] * fTemp51;
			fRec25[0] = ((std::fabs(fTemp52) > 1.1754944e-38f) ? fTemp52 : 0.0f);
			float fTemp53 = fTemp0 - fConst98 * (fConst99 * fRec28[2] + fConst100 * fRec28[1]);
			fRec28[0] = ((std::fabs(fTemp53) > 1.1754944e-38f) ? fTemp53 : 0.0f);
			float fTemp54 = std::fabs(fConst97 * (fRec28[0] - fRec28[2]));
			float fTemp55 = ((fTemp54 > fRec27[1]) ? fConst9 : fConst8);
			float fTemp56 = fTemp54 * (1.0f - fTemp55) + fRec27[1] * fTemp55;
			fRec27[0] = ((std::fabs(fTemp56) > 1.1754944e-38f) ? fTemp56 : 0.0f);
			float fTemp57 = fTemp0 - fConst105 * (fConst106 * fRec30[2] + fConst107 * fRec30[1]);
			fRec30[0] = ((std::fabs(fTemp57) > 1.1754944e-38f) ? fTemp57 : 0.0f);
			float fTemp58 = std::fabs(fConst104 * (fRec30[0] - fRec30[2]));
			float fTemp59 = ((fTemp58 > fRec29[1]) ? fConst9 : fConst8);
			float fTemp60 = fTemp58 * (1.0f - fTemp59) + fRec29[1] * fTemp59;
			fRec29[0] = ((std::fabs(fTemp60) > 1.1754944e-38f) ? fTemp60 : 0.0f);
			float fTemp61 = fTemp0 - fConst112 * (fConst113 * fRec32[2] + fConst114 * fRec32[1]);
			fRec32[0] = ((std::fabs(fTemp61) > 1.1754944e-38f) ? fTemp61 : 0.0f);
			float fTemp62 = std::fabs(fConst111 * (fRec32[0] - fRec32[2]));
			float fTemp63 = ((fTemp62 > fRec31[1]) ? fConst9 : fConst8);
			float fTemp64 = fTemp62 * (1.0f - fTemp63) + fRec31[1] * fTemp63;
			fRec31[0] = ((std::fabs(fTemp64) > 1.1754944e-38f) ? fTemp64 : 0.0f);
			float fTemp65 = fTemp0 - fConst119 * (fConst120 * fRec34[2] + fConst121 * fRec34[1]);
			fRec34[0] = ((std::fabs(fTemp65) > 1.1754944e-38f) ? fTemp65 : 0.0f);
			float fTemp66 = std::fabs(fConst118 * (fRec34[0] - fRec34[2]));
			float fTemp67 = ((fTemp66 > fRec33[1]) ? fConst9 : fConst8);
			float fTemp68 = fTemp66 * (1.0f - fTemp67) + fRec33[1] * fTemp67;
			fRec33[0] = ((std::fabs(fTemp68) > 1.1754944e-38f) ? fTemp68 : 0.0f);
			float fTemp69 = fTemp0 - fConst126 * (fConst127 * fRec36[2] + fConst128 * fRec36[1]);
			fRec36[0] = ((std::fabs(fTemp69) > 1.1754944e-38f) ? fTemp69 : 0.0f);
			float fTemp70 = std::fabs(fConst125 * (fRec36[0] - fRec36[2]));
			float fTemp71 = ((fTemp70 > fRec35[1]) ? fConst9 : fConst8);
			float fTemp72 = fTemp70 * (1.0f - fTemp71) + fRec35[1] * fTemp71;
			fRec35[0] = ((std::fabs(fTemp72) > 1.1754944e-38f) ? fTemp72 : 0.0f);
			float fTemp73 = fTemp0 - fConst133 * (fConst134 * fRec38[2] + fConst135 * fRec38[1]);
			fRec38[0] = ((std::fabs(fTemp73) > 1.1754944e-38f) ? fTemp73 : 0.0f);
			float fTemp74 = std::fabs(fConst132 * (fRec38[0] - fRec38[2]));
			float fTemp75 = ((fTemp74 > fRec37[1]) ? fConst9 : fConst8);
			float fTemp76 = fTemp74 * (1.0f - fTemp75) + fRec37[1] * fTemp75;
			fRec37[0] = ((std::fabs(fTemp76) > 1.1754944e-38f) ? fTemp76 : 0.0f);
			float fTemp77 = fTemp0 - fConst140 * (fConst141 * fRec40[2] + fConst142 * fRec40[1]);
			fRec40[0] = ((std::fabs(fTemp77) > 1.1754944e-38f) ? fTemp77 : 0.0f);
			float fTemp78 = std::fabs(fConst139 * (fRec40[0] - fRec40[2]));
			float fTemp79 = ((fTemp78 > fRec39[1]) ? fConst9 : fConst8);
			float fTemp80 = fTemp78 * (1.0f - fTemp79) + fRec39[1] * fTemp79;
			fRec39[0] = ((std::fabs(fTemp80) > 1.1754944e-38f) ? fTemp80 : 0.0f);
			float fTemp81 = fTemp0 - fConst147 * (fConst148 * fRec42[2] + fConst149 * fRec42[1]);
			fRec42[0] = ((std::fabs(fTemp81) > 1.1754944e-38f) ? fTemp81 : 0.0f);
			float fTemp82 = std::fabs(fConst146 * (fRec42[0] - fRec42[2]));
			float fTemp83 = ((fTemp82 > fRec41[1]) ? fConst9 : fConst8);
			float fTemp84 = fTemp82 * (1.0f - fTemp83) + fRec41[1] * fTemp83;
			fRec41[0] = ((std::fabs(fTemp84) > 1.1754944e-38f) ? fTemp84 : 0.0f);
			float fTemp85 = fTemp0 - fConst154 * (fConst155 * fRec44[2] + fConst156 * fRec44[1]);
			fRec44[0] = ((std::fabs(fTemp85) > 1.1754944e-38f) ? fTemp85 : 0.0f);
			float fTemp86 = std::fabs(fConst153 * (fRec44[0] - fRec44[2]));
			float fTemp87 = ((fTemp86 > fRec43[1]) ? fConst9 : fConst8);
			float fTemp88 = fTemp86 * (1.0f - fTemp87) + fRec43[1] * fTemp87;
			fRec43[0] = ((std::fabs(fTemp88) > 1.1754944e-38f) ? fTemp88 : 0.0f);
			float fTemp89 = fTemp0 - fConst161 * (fConst162 * fRec46[2] + fConst163 * fRec46[1]);
			fRec46[0] = ((std::fabs(fTemp89) > 1.1754944e-38f) ? fTemp89 : 0.0f);
			float fTemp90 = std::fabs(fConst160 * (fRec46[0] - fRec46[2]));
			float fTemp91 = ((fTemp90 > fRec45[1]) ? fConst9 : fConst8);
			float fTemp92 = fTemp90 * (1.0f - fTemp91) + fRec45[1] * fTemp91;
			fRec45[0] = ((std::fabs(fTemp92) > 1.1754944e-38f) ? fTemp92 : 0.0f);
			float fTemp93 = fTemp0 - fConst168 * (fConst169 * fRec48[2] + fConst170 * fRec48[1]);
			fRec48[0] = ((std::fabs(fTemp93) > 1.1754944e-38f) ? fTemp93 : 0.0f);
			float fTemp94 = std::fabs(fConst167 * (fRec48[0] - fRec48[2]));
			float fTemp95 = ((fTemp94 > fRec47[1]) ? fConst9 : fConst8);
			float fTemp96 = fTemp94 * (1.0f - fTemp95) + fRec47[1] * fTemp95;
			fRec47[0] = ((std::fabs(fTemp96) > 1.1754944e-38f) ? fTemp96 : 0.0f);
			float fTemp97 = fTemp0 - fConst175 * (fConst176 * fRec50[2] + fConst177 * fRec50[1]);
			fRec50[0] = ((std::fabs(fTemp97) > 1.1754944e-38f) ? fTemp97 : 0.0f);
			float fTemp98 = std::fabs(fConst174 * (fRec50[0] - fRec50[2]));
			float fTemp99 = ((fTemp98 > fRec49[1]) ? fConst9 : fConst8);
			float fTemp100 = fTemp98 * (1.0f - fTemp99) + fRec49[1] * fTemp99;
			fRec49[0] = ((std::fabs(fTemp100) > 1.1754944e-38f) ? fTemp100 : 0.0f);
			float fTemp101 = fTemp0 - fConst182 * (fConst183 * fRec52[2] + fConst184 * fRec52[1]);
			fRec52[0] = ((std::fabs(fTemp101) > 1.1754944e-38f) ? fTemp101 : 0.0f);
			float fTemp102 = std::fabs(fConst181 * (fRec52[0] - fRec52[2]));
			float fTemp103 = ((fTemp102 > fRec51[1]) ? fConst9 : fConst8);
			float fTemp104 = fTemp102 * (1.0f - fTemp103) + fRec51[1] * fTemp103;
			fRec51[0] = ((std::fabs(fTemp104) > 1.1754944e-38f) ? fTemp104 : 0.0f);
			float fTemp105 = fTemp0 - fConst189 * (fConst190 * fRec54[2] + fConst191 * fRec54[1]);
			fRec54[0] = ((std::fabs(fTemp105) > 1.1754944e-38f) ? fTemp105 : 0.0f);
			float fTemp106 = std::fabs(fConst188 * (fRec54[0] - fRec54[2]));
			float fTemp107 = ((fTemp106 > fRec53[1]) ? fConst9 : fConst8);
			float fTemp108 = fTemp106 * (1.0f - fTemp107) + fRec53[1] * fTemp107;
			fRec53[0] = ((std::fabs(fTemp108) > 1.1754944e-38f) ? fTemp108 : 0.0f);
			float fTemp109 = fTemp0 - fConst196 * (fConst197 * fRec56[2] + fConst198 * fRec56[1]);
			fRec56[0] = ((std::fabs(fTemp109) > 1.1754944e-38f) ? fTemp109 : 0.0f);
			float fTemp110 = std::fabs(fConst195 * (fRec56[0] - fRec56[2]));
			float fTemp111 = ((fTemp110 > fRec55[1]) ? fConst9 : fConst8);
			float fTemp112 = fTemp110 * (1.0f - fTemp111) + fRec55[1] * fTemp111;
			fRec55[0] = ((std::fabs(fTemp112) > 1.1754944e-38f) ? fTemp112 : 0.0f);
			float fTemp113 = fTemp0 - fConst203 * (fConst204 * fRec58[2] + fConst205 * fRec58[1]);
			fRec58[0] = ((std::fabs(fTemp113) > 1.1754944e-38f) ? fTemp113 : 0.0f);
			float fTemp114 = std::fabs(fConst202 * (fRec58[0] - fRec58[2]));
			float fTemp115 = ((fTemp114 > fRec57[1]) ? fConst9 : fConst8);
			float fTemp116 = fTemp114 * (1.0f - fTemp115) + fRec57[1] * fTemp115;
			fRec57[0] = ((std::fabs(fTemp116) > 1.1754944e-38f) ? fTemp116 : 0.0f);
			float fTemp117 = fTemp0 - fConst210 * (fConst211 * fRec60[2] + fConst212 * fRec60[1]);
			fRec60[0] = ((std::fabs(fTemp117) > 1.1754944e-38f) ? fTemp117 : 0.0f);
			float fTemp118 = std::fabs(fConst209 * (fRec60[0] - fRec60[2]));
			float fTemp119 = ((fTemp118 > fRec59[1]) ? fConst9 : fConst8);
			float fTemp120 = fTemp118 * (1.0f - fTemp119) + fRec59[1] * fTemp119;
			fRec59[0] = ((std::fabs(fTemp120) > 1.1754944e-38f) ? fTemp120 : 0.0f);
			float fTemp121 = fTemp0 - fConst217 * (fConst218 * fRec62[2] + fConst219 * fRec62[1]);
			fRec62[0] = ((std::fabs(fTemp121) > 1.1754944e-38f) ? fTemp121 : 0.0f);
			float fTemp122 = std::fabs(fConst216 * (fRec62[0] - fRec62[2]));
			float fTemp123 = ((fTemp122 > fRec61[1]) ? fConst9 : fConst8);
			float fTemp124 = fTemp122 * (1.0f - fTemp123) + fRec61[1] * fTemp123;
			fRec61[0] = ((std::fabs(fTemp124) > 1.1754944e-38f) ? fTemp124 : 0.0f);
			float fTemp125 = fTemp0 - fConst224 * (fConst225 * fRec64[2] + fConst226 * fRec64[1]);
			fRec64[0] = ((std::fabs(fTemp125) > 1.1754944e-38f) ? fTemp125 : 0.0f);
			float fTemp126 = std::fabs(fConst223 * (fRec64[0] - fRec64[2]));
			float fTemp127 = ((fTemp126 > fRec63[1]) ? fConst9 : fConst8);
			float fTemp128 = fTemp126 * (1.0f - fTemp127) + fRec63[1] * fTemp127;
			fRec63[0] = ((std::fabs(fTemp128) > 1.1754944e-38f) ? fTemp128 : 0.0f);
			int iTemp129 = fRec61[0] > fRec63[0];
			float fTemp130 = ((iTemp129) ? fRec61[0] : fRec63[0]);
			int iTemp131 = fRec59[0] > fTemp130;
			float fTemp132 = ((iTemp131) ? fRec59[0] : fTemp130);
			int iTemp133 = fRec57[0] > fTemp132;
			float fTemp134 = ((iTemp133) ? fRec57[0] : fTemp132);
			int iTemp135 = fRec55[0] > fTemp134;
			float fTemp136 = ((iTemp135) ? fRec55[0] : fTemp134);
			int iTemp137 = fRec53[0] > fTemp136;
			float fTemp138 = ((iTemp137) ? fRec53[0] : fTemp136);
			int iTemp139 = fRec51[0] > fTemp138;
			float fTemp140 = ((iTemp139) ? fRec51[0] : fTemp138);
			int iTemp141 = fRec49[0] > fTemp140;
			float fTemp142 = ((iTemp141) ? fRec49[0] : fTemp140);
			int iTemp143 = fRec47[0] > fTemp142;
			float fTemp144 = ((iTemp143) ? fRec47[0] : fTemp142);
			int iTemp145 = fRec45[0] > fTemp144;
			float fTemp146 = ((iTemp145) ? fRec45[0] : fTemp144);
			int iTemp147 = fRec43[0] > fTemp146;
			float fTemp148 = ((iTemp147) ? fRec43[0] : fTemp146);
			int iTemp149 = fRec41[0] > fTemp148;
			float fTemp150 = ((iTemp149) ? fRec41[0] : fTemp148);
			int iTemp151 = fRec39[0] > fTemp150;
			float fTemp152 = ((iTemp151) ? fRec39[0] : fTemp150);
			int iTemp153 = fRec37[0] > fTemp152;
			float fTemp154 = ((iTemp153) ? fRec37[0] : fTemp152);
			int iTemp155 = fRec35[0] > fTemp154;
			float fTemp156 = ((iTemp155) ? fRec35[0] : fTemp154);
			int iTemp157 = fRec33[0] > fTemp156;
			float fTemp158 = ((iTemp157) ? fRec33[0] : fTemp156);
			int iTemp159 = fRec31[0] > fTemp158;
			float fTemp160 = ((iTemp159) ? fRec31[0] : fTemp158);
			int iTemp161 = fRec29[0] > fTemp160;
			float fTemp162 = ((iTemp161) ? fRec29[0] : fTemp160);
			int iTemp163 = fRec27[0] > fTemp162;
			float fTemp164 = ((iTemp163) ? fRec27[0] : fTemp162);
			int iTemp165 = fRec25[0] > fTemp164;
			float fTemp166 = ((iTemp165) ? fRec25[0] : fTemp164);
			int iTemp167 = fRec23[0] > fTemp166;
			float fTemp168 = ((iTemp167) ? fRec23[0] : fTemp166);
			int iTemp169 = fRec21[0] > fTemp168;
			float fTemp170 = ((iTemp169) ? fRec21[0] : fTemp168);
			int iTemp171 = fRec19[0] > fTemp170;
			float fTemp172 = ((iTemp171) ? fRec19[0] : fTemp170);
			int iTemp173 = fRec17[0] > fTemp172;
			float fTemp174 = ((iTemp173) ? fRec17[0] : fTemp172);
			int iTemp175 = fRec15[0] > fTemp174;
			float fTemp176 = ((iTemp175) ? fRec15[0] : fTemp174);
			int iTemp177 = fRec13[0] > fTemp176;
			float fTemp178 = ((iTemp177) ? fRec13[0] : fTemp176);
			int iTemp179 = fRec11[0] > fTemp178;
			float fTemp180 = ((iTemp179) ? fRec11[0] : fTemp178);
			int iTemp181 = fRec9[0] > fTemp180;
			float fTemp182 = ((iTemp181) ? fRec9[0] : fTemp180);
			int iTemp183 = fRec7[0] > fTemp182;
			float fTemp184 = ((iTemp183) ? fRec7[0] : fTemp182);
			int iTemp185 = fRec5[0] > fTemp184;
			float fTemp186 = ((iTemp185) ? fRec5[0] : fTemp184);
			int iTemp187 = fRec3[0] > fTemp186;
			float fTemp188 = ((iTemp187) ? fRec3[0] : fTemp186);
			int iTemp189 = fRec1[0] > fTemp188;
			float fTemp190 = 2e+01f * std::log10((1e-09f + ((iTemp189) ? fRec1[0] : fTemp188)) / (1e-09f + 0.03125f * (fRec63[0] + fRec61[0] + fRec59[0] + fRec57[0] + fRec55[0] + fRec53[0] + fRec51[0] + fRec49[0] + fRec47[0] + fRec45[0] + fRec43[0] + fRec41[0] + fRec39[0] + fRec37[0] + fRec35[0] + fRec33[0] + fRec31[0] + fRec29[0] + fRec27[0] + fRec25[0] + fRec23[0] + fRec21[0] + fRec19[0] + fRec17[0] + fRec15[0] + fRec13[0] + fRec11[0] + fRec9[0] + fRec7[0] + fRec5[0] + fRec1[0] + fRec3[0])));
			float fTemp191 = static_cast<float>(std::abs(fTemp190 > 15.0f));
			float fTemp192 = ((fTemp191 > fRec0[1]) ? fConst228 : fConst227);
			float fTemp193 = fTemp191 * (1.0f - fTemp192) + fRec0[1] * fTemp192;
			fRec0[0] = ((std::fabs(fTemp193) > 1.1754944e-38f) ? fTemp193 : 0.0f);
			output0[i0] = static_cast<FAUSTFLOAT>(fRec0[0]);
			output1[i0] = static_cast<FAUSTFLOAT>(((iTemp189) ? 6e+03f : ((iTemp187) ? 5326.8677f : ((iTemp185) ? 4729.253f : ((iTemp183) ? 4198.684f : ((iTemp181) ? 3727.639f : ((iTemp179) ? 3309.4397f : ((iTemp177) ? 2938.158f : ((iTemp175) ? 2608.5295f : ((iTemp173) ? 2315.8818f : ((iTemp171) ? 2056.066f : ((iTemp169) ? 1825.3986f : ((iTemp167) ? 1620.6094f : ((iTemp165) ? 1438.7953f : ((iTemp163) ? 1277.3787f : ((iTemp161) ? 1134.0712f : ((iTemp159) ? 1006.84106f : ((iTemp157) ? 893.8848f : ((iTemp155) ? 793.601f : ((iTemp153) ? 704.56793f : ((iTemp151) ? 625.5233f : ((iTemp149) ? 555.3467f : ((iTemp147) ? 493.043f : ((iTemp145) ? 437.72913f : ((iTemp143) ? 388.62085f : ((iTemp141) ? 345.02197f : ((iTemp139) ? 306.3144f : ((iTemp137) ? 271.94934f : ((iTemp135) ? 241.4397f : ((iTemp133) ? 214.35287f : ((iTemp131) ? 190.3049f : ((iTemp129) ? 168.95483f : 1.5e+02f))))))))))))))))))))))))))))))));
			output2[i0] = static_cast<FAUSTFLOAT>(fTemp190);
			fRec2[2] = fRec2[1];
			fRec2[1] = fRec2[0];
			fRec1[1] = fRec1[0];
			fRec4[2] = fRec4[1];
			fRec4[1] = fRec4[0];
			fRec3[1] = fRec3[0];
			fRec6[2] = fRec6[1];
			fRec6[1] = fRec6[0];
			fRec5[1] = fRec5[0];
			fRec8[2] = fRec8[1];
			fRec8[1] = fRec8[0];
			fRec7[1] = fRec7[0];
			fRec10[2] = fRec10[1];
			fRec10[1] = fRec10[0];
			fRec9[1] = fRec9[0];
			fRec12[2] = fRec12[1];
			fRec12[1] = fRec12[0];
			fRec11[1] = fRec11[0];
			fRec14[2] = fRec14[1];
			fRec14[1] = fRec14[0];
			fRec13[1] = fRec13[0];
			fRec16[2] = fRec16[1];
			fRec16[1] = fRec16[0];
			fRec15[1] = fRec15[0];
			fRec18[2] = fRec18[1];
			fRec18[1] = fRec18[0];
			fRec17[1] = fRec17[0];
			fRec20[2] = fRec20[1];
			fRec20[1] = fRec20[0];
			fRec19[1] = fRec19[0];
			fRec22[2] = fRec22[1];
			fRec22[1] = fRec22[0];
			fRec21[1] = fRec21[0];
			fRec24[2] = fRec24[1];
			fRec24[1] = fRec24[0];
			fRec23[1] = fRec23[0];
			fRec26[2] = fRec26[1];
			fRec26[1] = fRec26[0];
			fRec25[1] = fRec25[0];
			fRec28[2] = fRec28[1];
			fRec28[1] = fRec28[0];
			fRec27[1] = fRec27[0];
			fRec30[2] = fRec30[1];
			fRec30[1] = fRec30[0];
			fRec29[1] = fRec29[0];
			fRec32[2] = fRec32[1];
			fRec32[1] = fRec32[0];
			fRec31[1] = fRec31[0];
			fRec34[2] = fRec34[1];
			fRec34[1] = fRec34[0];
			fRec33[1] = fRec33[0];
			fRec36[2] = fRec36[1];
			fRec36[1] = fRec36[0];
			fRec35[1] = fRec35[0];
			fRec38[2] = fRec38[1];
			fRec38[1] = fRec38[0];
			fRec37[1] = fRec37[0];
			fRec40[2] = fRec40[1];
			fRec40[1] = fRec40[0];
			fRec39[1] = fRec39[0];
			fRec42[2] = fRec42[1];
			fRec42[1] = fRec42[0];
			fRec41[1] = fRec41[0];
			fRec44[2] = fRec44[1];
			fRec44[1] = fRec44[0];
			fRec43[1] = fRec43[0];
			fRec46[2] = fRec46[1];
			fRec46[1] = fRec46[0];
			fRec45[1] = fRec45[0];
			fRec48[2] = fRec48[1];
			fRec48[1] = fRec48[0];
			fRec47[1] = fRec47[0];
			fRec50[2] = fRec50[1];
			fRec50[1] = fRec50[0];
			fRec49[1] = fRec49[0];
			fRec52[2] = fRec52[1];
			fRec52[1] = fRec52[0];
			fRec51[1] = fRec51[0];
			fRec54[2] = fRec54[1];
			fRec54[1] = fRec54[0];
			fRec53[1] = fRec53[0];
			fRec56[2] = fRec56[1];
			fRec56[1] = fRec56[0];
			fRec55[1] = fRec55[0];
			fRec58[2] = fRec58[1];
			fRec58[1] = fRec58[0];
			fRec57[1] = fRec57[0];
			fRec60[2] = fRec60[1];
			fRec60[1] = fRec60[0];
			fRec59[1] = fRec59[0];
			fRec62[2] = fRec62[1];
			fRec62[1] = fRec62[0];
			fRec61[1] = fRec61[0];
			fRec64[2] = fRec64[1];
			fRec64[1] = fRec64[0];
			fRec63[1] = fRec63[0];
			fRec0[1] = fRec0[0];
		}
	}

};

} // namespace mutap_faust

#endif
