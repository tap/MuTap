/* ------------------------------------------------------------
name: "icc_howl_detect"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn icc_howl_detect_f64 -es 1 -mcd 16 -mdd 1024 -mdy 33 -double -ftz 1
------------------------------------------------------------ */

#ifndef  __icc_howl_detect_f64_H__
#define  __icc_howl_detect_f64_H__

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
#define FAUSTCLASS icc_howl_detect_f64
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

static double icc_howl_detect_f64_faustpower2_f(double value) {
	return value * value;
}

class icc_howl_detect_f64 : public dsp {
	
 private:
	
	int fSampleRate;
	double fConst0;
	double fConst1;
	double fConst2;
	double fConst3;
	double fConst4;
	double fConst5;
	double fConst6;
	double fConst7;
	double fRec2[3];
	double fConst8;
	double fConst9;
	double fRec1[2];
	double fConst10;
	double fConst11;
	double fConst12;
	double fConst13;
	double fConst14;
	double fConst15;
	double fConst16;
	double fRec4[3];
	double fRec3[2];
	double fConst17;
	double fConst18;
	double fConst19;
	double fConst20;
	double fConst21;
	double fConst22;
	double fConst23;
	double fRec6[3];
	double fRec5[2];
	double fConst24;
	double fConst25;
	double fConst26;
	double fConst27;
	double fConst28;
	double fConst29;
	double fConst30;
	double fRec8[3];
	double fRec7[2];
	double fConst31;
	double fConst32;
	double fConst33;
	double fConst34;
	double fConst35;
	double fConst36;
	double fConst37;
	double fRec10[3];
	double fRec9[2];
	double fConst38;
	double fConst39;
	double fConst40;
	double fConst41;
	double fConst42;
	double fConst43;
	double fConst44;
	double fRec12[3];
	double fRec11[2];
	double fConst45;
	double fConst46;
	double fConst47;
	double fConst48;
	double fConst49;
	double fConst50;
	double fConst51;
	double fRec14[3];
	double fRec13[2];
	double fConst52;
	double fConst53;
	double fConst54;
	double fConst55;
	double fConst56;
	double fConst57;
	double fConst58;
	double fRec16[3];
	double fRec15[2];
	double fConst59;
	double fConst60;
	double fConst61;
	double fConst62;
	double fConst63;
	double fConst64;
	double fConst65;
	double fRec18[3];
	double fRec17[2];
	double fConst66;
	double fConst67;
	double fConst68;
	double fConst69;
	double fConst70;
	double fConst71;
	double fConst72;
	double fRec20[3];
	double fRec19[2];
	double fConst73;
	double fConst74;
	double fConst75;
	double fConst76;
	double fConst77;
	double fConst78;
	double fConst79;
	double fRec22[3];
	double fRec21[2];
	double fConst80;
	double fConst81;
	double fConst82;
	double fConst83;
	double fConst84;
	double fConst85;
	double fConst86;
	double fRec24[3];
	double fRec23[2];
	double fConst87;
	double fConst88;
	double fConst89;
	double fConst90;
	double fConst91;
	double fConst92;
	double fConst93;
	double fRec26[3];
	double fRec25[2];
	double fConst94;
	double fConst95;
	double fConst96;
	double fConst97;
	double fConst98;
	double fConst99;
	double fConst100;
	double fRec28[3];
	double fRec27[2];
	double fConst101;
	double fConst102;
	double fConst103;
	double fConst104;
	double fConst105;
	double fConst106;
	double fConst107;
	double fRec30[3];
	double fRec29[2];
	double fConst108;
	double fConst109;
	double fConst110;
	double fConst111;
	double fConst112;
	double fConst113;
	double fConst114;
	double fRec32[3];
	double fRec31[2];
	double fConst115;
	double fConst116;
	double fConst117;
	double fConst118;
	double fConst119;
	double fConst120;
	double fConst121;
	double fRec34[3];
	double fRec33[2];
	double fConst122;
	double fConst123;
	double fConst124;
	double fConst125;
	double fConst126;
	double fConst127;
	double fConst128;
	double fRec36[3];
	double fRec35[2];
	double fConst129;
	double fConst130;
	double fConst131;
	double fConst132;
	double fConst133;
	double fConst134;
	double fConst135;
	double fRec38[3];
	double fRec37[2];
	double fConst136;
	double fConst137;
	double fConst138;
	double fConst139;
	double fConst140;
	double fConst141;
	double fConst142;
	double fRec40[3];
	double fRec39[2];
	double fConst143;
	double fConst144;
	double fConst145;
	double fConst146;
	double fConst147;
	double fConst148;
	double fConst149;
	double fRec42[3];
	double fRec41[2];
	double fConst150;
	double fConst151;
	double fConst152;
	double fConst153;
	double fConst154;
	double fConst155;
	double fConst156;
	double fRec44[3];
	double fRec43[2];
	double fConst157;
	double fConst158;
	double fConst159;
	double fConst160;
	double fConst161;
	double fConst162;
	double fConst163;
	double fRec46[3];
	double fRec45[2];
	double fConst164;
	double fConst165;
	double fConst166;
	double fConst167;
	double fConst168;
	double fConst169;
	double fConst170;
	double fRec48[3];
	double fRec47[2];
	double fConst171;
	double fConst172;
	double fConst173;
	double fConst174;
	double fConst175;
	double fConst176;
	double fConst177;
	double fRec50[3];
	double fRec49[2];
	double fConst178;
	double fConst179;
	double fConst180;
	double fConst181;
	double fConst182;
	double fConst183;
	double fConst184;
	double fRec52[3];
	double fRec51[2];
	double fConst185;
	double fConst186;
	double fConst187;
	double fConst188;
	double fConst189;
	double fConst190;
	double fConst191;
	double fRec54[3];
	double fRec53[2];
	double fConst192;
	double fConst193;
	double fConst194;
	double fConst195;
	double fConst196;
	double fConst197;
	double fConst198;
	double fRec56[3];
	double fRec55[2];
	double fConst199;
	double fConst200;
	double fConst201;
	double fConst202;
	double fConst203;
	double fConst204;
	double fConst205;
	double fRec58[3];
	double fRec57[2];
	double fConst206;
	double fConst207;
	double fConst208;
	double fConst209;
	double fConst210;
	double fConst211;
	double fConst212;
	double fRec60[3];
	double fRec59[2];
	double fConst213;
	double fConst214;
	double fConst215;
	double fConst216;
	double fConst217;
	double fConst218;
	double fConst219;
	double fRec62[3];
	double fRec61[2];
	double fConst220;
	double fConst221;
	double fConst222;
	double fConst223;
	double fConst224;
	double fConst225;
	double fConst226;
	double fRec64[3];
	double fRec63[2];
	double fConst227;
	double fConst228;
	double fRec0[2];
	
 public:
	icc_howl_detect_f64() {
	}
	
	icc_howl_detect_f64(const icc_howl_detect_f64&) = default;
	
	virtual ~icc_howl_detect_f64() = default;
	
	icc_howl_detect_f64& operator=(const icc_howl_detect_f64&) = default;
	
	void metadata(Meta* m) { 
		m->declare("analyzers.lib/amp_follower_ar:author", "Jonatan Liljedahl, revised by Romain Michon");
		m->declare("analyzers.lib/name", "Faust Analyzer Library");
		m->declare("analyzers.lib/version", "1.4.0");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn icc_howl_detect_f64 -es 1 -mcd 16 -mdd 1024 -mdy 33 -double -ftz 1");
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
		fConst0 = std::min<double>(1.92e+05, std::max<double>(1.0, static_cast<double>(fSampleRate)));
		fConst1 = std::tan(18849.55592153876 / fConst0);
		fConst2 = 1.0 / fConst1;
		fConst3 = (fConst2 + 0.07142857142857142) / fConst1 + 1.0;
		fConst4 = 1.0 / (fConst1 * fConst3);
		fConst5 = 1.0 / fConst3;
		fConst6 = (fConst2 + -0.07142857142857142) / fConst1 + 1.0;
		fConst7 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst1));
		fConst8 = std::exp(-(2e+01 / fConst0));
		fConst9 = std::exp(-(2e+02 / fConst0));
		fConst10 = std::tan(16734.84786265819 / fConst0);
		fConst11 = 1.0 / fConst10;
		fConst12 = (fConst11 + 0.07142857142857142) / fConst10 + 1.0;
		fConst13 = 1.0 / (fConst10 * fConst12);
		fConst14 = 1.0 / fConst12;
		fConst15 = (fConst11 + -0.07142857142857142) / fConst10 + 1.0;
		fConst16 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst10));
		fConst17 = std::tan(14857.386251010059 / fConst0);
		fConst18 = 1.0 / fConst17;
		fConst19 = (fConst18 + 0.07142857142857142) / fConst17 + 1.0;
		fConst20 = 1.0 / (fConst17 * fConst19);
		fConst21 = 1.0 / fConst19;
		fConst22 = (fConst18 + -0.07142857142857142) / fConst17 + 1.0;
		fConst23 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst17));
		fConst24 = std::tan(13190.554704967593 / fConst0);
		fConst25 = 1.0 / fConst24;
		fConst26 = (fConst25 + 0.07142857142857142) / fConst24 + 1.0;
		fConst27 = 1.0 / (fConst24 * fConst26);
		fConst28 = 1.0 / fConst26;
		fConst29 = (fConst25 + -0.07142857142857142) / fConst24 + 1.0;
		fConst30 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst24));
		fConst31 = std::tan(11710.722901406309 / fConst0);
		fConst32 = 1.0 / fConst31;
		fConst33 = (fConst32 + 0.07142857142857142) / fConst31 + 1.0;
		fConst34 = 1.0 / (fConst31 * fConst33);
		fConst35 = 1.0 / fConst33;
		fConst36 = (fConst32 + -0.07142857142857142) / fConst31 + 1.0;
		fConst37 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst31));
		fConst38 = std::tan(10396.911573542435 / fConst0);
		fConst39 = 1.0 / fConst38;
		fConst40 = (fConst39 + 0.07142857142857142) / fConst38 + 1.0;
		fConst41 = 1.0 / (fConst38 * fConst40);
		fConst42 = 1.0 / fConst40;
		fConst43 = (fConst39 + -0.07142857142857142) / fConst38 + 1.0;
		fConst44 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst38));
		fConst45 = std::tan(9230.49509224402 / fConst0);
		fConst46 = 1.0 / fConst45;
		fConst47 = (fConst46 + 0.07142857142857142) / fConst45 + 1.0;
		fConst48 = 1.0 / (fConst45 * fConst47);
		fConst49 = 1.0 / fConst47;
		fConst50 = (fConst46 + -0.07142857142857142) / fConst45 + 1.0;
		fConst51 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst45));
		fConst52 = std::tan(8194.937414372078 / fConst0);
		fConst53 = 1.0 / fConst52;
		fConst54 = (fConst53 + 0.07142857142857142) / fConst52 + 1.0;
		fConst55 = 1.0 / (fConst52 * fConst54);
		fConst56 = 1.0 / fConst54;
		fConst57 = (fConst53 + -0.07142857142857142) / fConst52 + 1.0;
		fConst58 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst52));
		fConst59 = std::tan(7275.557654746429 / fConst0);
		fConst60 = 1.0 / fConst59;
		fConst61 = (fConst60 + 0.07142857142857142) / fConst59 + 1.0;
		fConst62 = 1.0 / (fConst59 * fConst61);
		fConst63 = 1.0 / fConst61;
		fConst64 = (fConst60 + -0.07142857142857142) / fConst59 + 1.0;
		fConst65 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst59));
		fConst66 = std::tan(6459.321958298971 / fConst0);
		fConst67 = 1.0 / fConst66;
		fConst68 = (fConst67 + 0.07142857142857142) / fConst66 + 1.0;
		fConst69 = 1.0 / (fConst66 * fConst68);
		fConst70 = 1.0 / fConst68;
		fConst71 = (fConst67 + -0.07142857142857142) / fConst66 + 1.0;
		fConst72 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst66));
		fConst73 = std::tan(5734.658721829259 / fConst0);
		fConst74 = 1.0 / fConst73;
		fConst75 = (fConst74 + 0.07142857142857142) / fConst73 + 1.0;
		fConst76 = 1.0 / (fConst73 * fConst75);
		fConst77 = 1.0 / fConst75;
		fConst78 = (fConst74 + -0.07142857142857142) / fConst73 + 1.0;
		fConst79 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst73));
		fConst80 = std::tan(5091.294545799795 / fConst0);
		fConst81 = 1.0 / fConst80;
		fConst82 = (fConst81 + 0.07142857142857142) / fConst80 + 1.0;
		fConst83 = 1.0 / (fConst80 * fConst82);
		fConst84 = 1.0 / fConst82;
		fConst85 = (fConst81 + -0.07142857142857142) / fConst80 + 1.0;
		fConst86 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst80));
		fConst87 = std::tan(4520.10859049382 / fConst0);
		fConst88 = 1.0 / fConst87;
		fConst89 = (fConst88 + 0.07142857142857142) / fConst87 + 1.0;
		fConst90 = 1.0 / (fConst87 * fConst89);
		fConst91 = 1.0 / fConst89;
		fConst92 = (fConst88 + -0.07142857142857142) / fConst87 + 1.0;
		fConst93 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst87));
		fConst94 = std::tan(4013.0032717732784 / fConst0);
		fConst95 = 1.0 / fConst94;
		fConst96 = (fConst95 + 0.07142857142857142) / fConst94 + 1.0;
		fConst97 = 1.0 / (fConst94 * fConst96);
		fConst98 = 1.0 / fConst96;
		fConst99 = (fConst95 + -0.07142857142857142) / fConst94 + 1.0;
		fConst100 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst94));
		fConst101 = std::tan(3562.789463317662 / fConst0);
		fConst102 = 1.0 / fConst101;
		fConst103 = (fConst102 + 0.07142857142857142) / fConst101 + 1.0;
		fConst104 = 1.0 / (fConst101 * fConst103);
		fConst105 = 1.0 / fConst103;
		fConst106 = (fConst102 + -0.07142857142857142) / fConst101 + 1.0;
		fConst107 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst101));
		fConst108 = std::tan(3163.0845778798307 / fConst0);
		fConst109 = 1.0 / fConst108;
		fConst110 = (fConst109 + 0.07142857142857142) / fConst108 + 1.0;
		fConst111 = 1.0 / (fConst108 * fConst110);
		fConst112 = 1.0 / fConst110;
		fConst113 = (fConst109 + -0.07142857142857142) / fConst108 + 1.0;
		fConst114 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst108));
		fConst115 = std::tan(2808.2220826780194 / fConst0);
		fConst116 = 1.0 / fConst115;
		fConst117 = (fConst116 + 0.07142857142857142) / fConst115 + 1.0;
		fConst118 = 1.0 / (fConst115 * fConst117);
		fConst119 = 1.0 / fConst117;
		fConst120 = (fConst116 + -0.07142857142857142) / fConst115 + 1.0;
		fConst121 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst115));
		fConst122 = std::tan(2493.171166142644 / fConst0);
		fConst123 = 1.0 / fConst122;
		fConst124 = (fConst123 + 0.07142857142857142) / fConst122 + 1.0;
		fConst125 = 1.0 / (fConst122 * fConst124);
		fConst126 = 1.0 / fConst124;
		fConst127 = (fConst123 + -0.07142857142857142) / fConst122 + 1.0;
		fConst128 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst122));
		fConst129 = std::tan(2213.465417150117 / fConst0);
		fConst130 = 1.0 / fConst129;
		fConst131 = (fConst130 + 0.07142857142857142) / fConst129 + 1.0;
		fConst132 = 1.0 / (fConst129 * fConst131);
		fConst133 = 1.0 / fConst131;
		fConst134 = (fConst130 + -0.07142857142857142) / fConst129 + 1.0;
		fConst135 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst129));
		fConst136 = std::tan(1965.1395056440444 / fConst0);
		fConst137 = 1.0 / fConst136;
		fConst138 = (fConst137 + 0.07142857142857142) / fConst136 + 1.0;
		fConst139 = 1.0 / (fConst136 * fConst138);
		fConst140 = 1.0 / fConst138;
		fConst141 = (fConst137 + -0.07142857142857142) / fConst136 + 1.0;
		fConst142 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst136));
		fConst143 = std::tan(1744.6729669781932 / fConst0);
		fConst144 = 1.0 / fConst143;
		fConst145 = (fConst144 + 0.07142857142857142) / fConst143 + 1.0;
		fConst146 = 1.0 / (fConst143 * fConst145);
		fConst147 = 1.0 / fConst145;
		fConst148 = (fConst144 + -0.07142857142857142) / fConst143 + 1.0;
		fConst149 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst143));
		fConst150 = std::tan(1548.9402930235765 / fConst0);
		fConst151 = 1.0 / fConst150;
		fConst152 = (fConst151 + 0.07142857142857142) / fConst150 + 1.0;
		fConst153 = 1.0 / (fConst150 * fConst152);
		fConst154 = 1.0 / fConst152;
		fConst155 = (fConst151 + -0.07142857142857142) / fConst150 + 1.0;
		fConst156 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst150));
		fConst157 = std::tan(1375.1666224916928 / fConst0);
		fConst158 = 1.0 / fConst157;
		fConst159 = (fConst158 + 0.07142857142857142) / fConst157 + 1.0;
		fConst160 = 1.0 / (fConst157 * fConst159);
		fConst161 = 1.0 / fConst159;
		fConst162 = (fConst158 + -0.07142857142857142) / fConst157 + 1.0;
		fConst163 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst157));
		fConst164 = std::tan(1220.8884023048818 / fConst0);
		fConst165 = 1.0 / fConst164;
		fConst166 = (fConst165 + 0.07142857142857142) / fConst164 + 1.0;
		fConst167 = 1.0 / (fConst164 * fConst166);
		fConst168 = 1.0 / fConst166;
		fConst169 = (fConst165 + -0.07142857142857142) / fConst164 + 1.0;
		fConst170 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst164));
		fConst171 = std::tan(1083.918462318243 / fConst0);
		fConst172 = 1.0 / fConst171;
		fConst173 = (fConst172 + 0.07142857142857142) / fConst171 + 1.0;
		fConst174 = 1.0 / (fConst171 * fConst173);
		fConst175 = 1.0 / fConst173;
		fConst176 = (fConst172 + -0.07142857142857142) / fConst171 + 1.0;
		fConst177 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst171));
		fConst178 = std::tan(962.3150082647372 / fConst0);
		fConst179 = 1.0 / fConst178;
		fConst180 = (fConst179 + 0.07142857142857142) / fConst178 + 1.0;
		fConst181 = 1.0 / (fConst178 * fConst180);
		fConst182 = 1.0 / fConst180;
		fConst183 = (fConst179 + -0.07142857142857142) / fConst178 + 1.0;
		fConst184 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst178));
		fConst185 = std::tan(854.3540933429261 / fConst0);
		fConst186 = 1.0 / fConst185;
		fConst187 = (fConst186 + 0.07142857142857142) / fConst185 + 1.0;
		fConst188 = 1.0 / (fConst185 * fConst187);
		fConst189 = 1.0 / fConst187;
		fConst190 = (fConst186 + -0.07142857142857142) / fConst185 + 1.0;
		fConst191 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst185));
		fConst192 = std::tan(758.5051781827858 / fConst0);
		fConst193 = 1.0 / fConst192;
		fConst194 = (fConst193 + 0.07142857142857142) / fConst192 + 1.0;
		fConst195 = 1.0 / (fConst192 * fConst194);
		fConst196 = 1.0 / fConst194;
		fConst197 = (fConst193 + -0.07142857142857142) / fConst192 + 1.0;
		fConst198 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst192));
		fConst199 = std::tan(673.4094327083301 / fConst0);
		fConst200 = 1.0 / fConst199;
		fConst201 = (fConst200 + 0.07142857142857142) / fConst199 + 1.0;
		fConst202 = 1.0 / (fConst199 * fConst201);
		fConst203 = 1.0 / fConst201;
		fConst204 = (fConst200 + -0.07142857142857142) / fConst199 + 1.0;
		fConst205 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst199));
		fConst206 = std::tan(597.8604722870784 / fConst0);
		fConst207 = 1.0 / fConst206;
		fConst208 = (fConst207 + 0.07142857142857142) / fConst206 + 1.0;
		fConst209 = 1.0 / (fConst206 * fConst208);
		fConst210 = 1.0 / fConst208;
		fConst211 = (fConst207 + -0.07142857142857142) / fConst206 + 1.0;
		fConst212 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst206));
		fConst213 = std::tan(530.7872550667748 / fConst0);
		fConst214 = 1.0 / fConst213;
		fConst215 = (fConst214 + 0.07142857142857142) / fConst213 + 1.0;
		fConst216 = 1.0 / (fConst213 * fConst215);
		fConst217 = 1.0 / fConst215;
		fConst218 = (fConst214 + -0.07142857142857142) / fConst213 + 1.0;
		fConst219 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst213));
		fConst220 = std::tan(471.23889803846896 / fConst0);
		fConst221 = 1.0 / fConst220;
		fConst222 = (fConst221 + 0.07142857142857142) / fConst220 + 1.0;
		fConst223 = 1.0 / (fConst220 * fConst222);
		fConst224 = 1.0 / fConst222;
		fConst225 = (fConst221 + -0.07142857142857142) / fConst220 + 1.0;
		fConst226 = 2.0 * (1.0 - 1.0 / icc_howl_detect_f64_faustpower2_f(fConst220));
		fConst227 = std::exp(-(1.25 / fConst0));
		fConst228 = std::exp(-(5.0 / fConst0));
	}
	
	virtual void instanceResetUserInterface() {
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 3; l0 = faust_wrap_add(l0, 1)) {
			fRec2[l0] = 0.0;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			fRec1[l1] = 0.0;
		}
		for (int l2 = 0; l2 < 3; l2 = faust_wrap_add(l2, 1)) {
			fRec4[l2] = 0.0;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fRec3[l3] = 0.0;
		}
		for (int l4 = 0; l4 < 3; l4 = faust_wrap_add(l4, 1)) {
			fRec6[l4] = 0.0;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec5[l5] = 0.0;
		}
		for (int l6 = 0; l6 < 3; l6 = faust_wrap_add(l6, 1)) {
			fRec8[l6] = 0.0;
		}
		for (int l7 = 0; l7 < 2; l7 = faust_wrap_add(l7, 1)) {
			fRec7[l7] = 0.0;
		}
		for (int l8 = 0; l8 < 3; l8 = faust_wrap_add(l8, 1)) {
			fRec10[l8] = 0.0;
		}
		for (int l9 = 0; l9 < 2; l9 = faust_wrap_add(l9, 1)) {
			fRec9[l9] = 0.0;
		}
		for (int l10 = 0; l10 < 3; l10 = faust_wrap_add(l10, 1)) {
			fRec12[l10] = 0.0;
		}
		for (int l11 = 0; l11 < 2; l11 = faust_wrap_add(l11, 1)) {
			fRec11[l11] = 0.0;
		}
		for (int l12 = 0; l12 < 3; l12 = faust_wrap_add(l12, 1)) {
			fRec14[l12] = 0.0;
		}
		for (int l13 = 0; l13 < 2; l13 = faust_wrap_add(l13, 1)) {
			fRec13[l13] = 0.0;
		}
		for (int l14 = 0; l14 < 3; l14 = faust_wrap_add(l14, 1)) {
			fRec16[l14] = 0.0;
		}
		for (int l15 = 0; l15 < 2; l15 = faust_wrap_add(l15, 1)) {
			fRec15[l15] = 0.0;
		}
		for (int l16 = 0; l16 < 3; l16 = faust_wrap_add(l16, 1)) {
			fRec18[l16] = 0.0;
		}
		for (int l17 = 0; l17 < 2; l17 = faust_wrap_add(l17, 1)) {
			fRec17[l17] = 0.0;
		}
		for (int l18 = 0; l18 < 3; l18 = faust_wrap_add(l18, 1)) {
			fRec20[l18] = 0.0;
		}
		for (int l19 = 0; l19 < 2; l19 = faust_wrap_add(l19, 1)) {
			fRec19[l19] = 0.0;
		}
		for (int l20 = 0; l20 < 3; l20 = faust_wrap_add(l20, 1)) {
			fRec22[l20] = 0.0;
		}
		for (int l21 = 0; l21 < 2; l21 = faust_wrap_add(l21, 1)) {
			fRec21[l21] = 0.0;
		}
		for (int l22 = 0; l22 < 3; l22 = faust_wrap_add(l22, 1)) {
			fRec24[l22] = 0.0;
		}
		for (int l23 = 0; l23 < 2; l23 = faust_wrap_add(l23, 1)) {
			fRec23[l23] = 0.0;
		}
		for (int l24 = 0; l24 < 3; l24 = faust_wrap_add(l24, 1)) {
			fRec26[l24] = 0.0;
		}
		for (int l25 = 0; l25 < 2; l25 = faust_wrap_add(l25, 1)) {
			fRec25[l25] = 0.0;
		}
		for (int l26 = 0; l26 < 3; l26 = faust_wrap_add(l26, 1)) {
			fRec28[l26] = 0.0;
		}
		for (int l27 = 0; l27 < 2; l27 = faust_wrap_add(l27, 1)) {
			fRec27[l27] = 0.0;
		}
		for (int l28 = 0; l28 < 3; l28 = faust_wrap_add(l28, 1)) {
			fRec30[l28] = 0.0;
		}
		for (int l29 = 0; l29 < 2; l29 = faust_wrap_add(l29, 1)) {
			fRec29[l29] = 0.0;
		}
		for (int l30 = 0; l30 < 3; l30 = faust_wrap_add(l30, 1)) {
			fRec32[l30] = 0.0;
		}
		for (int l31 = 0; l31 < 2; l31 = faust_wrap_add(l31, 1)) {
			fRec31[l31] = 0.0;
		}
		for (int l32 = 0; l32 < 3; l32 = faust_wrap_add(l32, 1)) {
			fRec34[l32] = 0.0;
		}
		for (int l33 = 0; l33 < 2; l33 = faust_wrap_add(l33, 1)) {
			fRec33[l33] = 0.0;
		}
		for (int l34 = 0; l34 < 3; l34 = faust_wrap_add(l34, 1)) {
			fRec36[l34] = 0.0;
		}
		for (int l35 = 0; l35 < 2; l35 = faust_wrap_add(l35, 1)) {
			fRec35[l35] = 0.0;
		}
		for (int l36 = 0; l36 < 3; l36 = faust_wrap_add(l36, 1)) {
			fRec38[l36] = 0.0;
		}
		for (int l37 = 0; l37 < 2; l37 = faust_wrap_add(l37, 1)) {
			fRec37[l37] = 0.0;
		}
		for (int l38 = 0; l38 < 3; l38 = faust_wrap_add(l38, 1)) {
			fRec40[l38] = 0.0;
		}
		for (int l39 = 0; l39 < 2; l39 = faust_wrap_add(l39, 1)) {
			fRec39[l39] = 0.0;
		}
		for (int l40 = 0; l40 < 3; l40 = faust_wrap_add(l40, 1)) {
			fRec42[l40] = 0.0;
		}
		for (int l41 = 0; l41 < 2; l41 = faust_wrap_add(l41, 1)) {
			fRec41[l41] = 0.0;
		}
		for (int l42 = 0; l42 < 3; l42 = faust_wrap_add(l42, 1)) {
			fRec44[l42] = 0.0;
		}
		for (int l43 = 0; l43 < 2; l43 = faust_wrap_add(l43, 1)) {
			fRec43[l43] = 0.0;
		}
		for (int l44 = 0; l44 < 3; l44 = faust_wrap_add(l44, 1)) {
			fRec46[l44] = 0.0;
		}
		for (int l45 = 0; l45 < 2; l45 = faust_wrap_add(l45, 1)) {
			fRec45[l45] = 0.0;
		}
		for (int l46 = 0; l46 < 3; l46 = faust_wrap_add(l46, 1)) {
			fRec48[l46] = 0.0;
		}
		for (int l47 = 0; l47 < 2; l47 = faust_wrap_add(l47, 1)) {
			fRec47[l47] = 0.0;
		}
		for (int l48 = 0; l48 < 3; l48 = faust_wrap_add(l48, 1)) {
			fRec50[l48] = 0.0;
		}
		for (int l49 = 0; l49 < 2; l49 = faust_wrap_add(l49, 1)) {
			fRec49[l49] = 0.0;
		}
		for (int l50 = 0; l50 < 3; l50 = faust_wrap_add(l50, 1)) {
			fRec52[l50] = 0.0;
		}
		for (int l51 = 0; l51 < 2; l51 = faust_wrap_add(l51, 1)) {
			fRec51[l51] = 0.0;
		}
		for (int l52 = 0; l52 < 3; l52 = faust_wrap_add(l52, 1)) {
			fRec54[l52] = 0.0;
		}
		for (int l53 = 0; l53 < 2; l53 = faust_wrap_add(l53, 1)) {
			fRec53[l53] = 0.0;
		}
		for (int l54 = 0; l54 < 3; l54 = faust_wrap_add(l54, 1)) {
			fRec56[l54] = 0.0;
		}
		for (int l55 = 0; l55 < 2; l55 = faust_wrap_add(l55, 1)) {
			fRec55[l55] = 0.0;
		}
		for (int l56 = 0; l56 < 3; l56 = faust_wrap_add(l56, 1)) {
			fRec58[l56] = 0.0;
		}
		for (int l57 = 0; l57 < 2; l57 = faust_wrap_add(l57, 1)) {
			fRec57[l57] = 0.0;
		}
		for (int l58 = 0; l58 < 3; l58 = faust_wrap_add(l58, 1)) {
			fRec60[l58] = 0.0;
		}
		for (int l59 = 0; l59 < 2; l59 = faust_wrap_add(l59, 1)) {
			fRec59[l59] = 0.0;
		}
		for (int l60 = 0; l60 < 3; l60 = faust_wrap_add(l60, 1)) {
			fRec62[l60] = 0.0;
		}
		for (int l61 = 0; l61 < 2; l61 = faust_wrap_add(l61, 1)) {
			fRec61[l61] = 0.0;
		}
		for (int l62 = 0; l62 < 3; l62 = faust_wrap_add(l62, 1)) {
			fRec64[l62] = 0.0;
		}
		for (int l63 = 0; l63 < 2; l63 = faust_wrap_add(l63, 1)) {
			fRec63[l63] = 0.0;
		}
		for (int l64 = 0; l64 < 2; l64 = faust_wrap_add(l64, 1)) {
			fRec0[l64] = 0.0;
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
	
	virtual icc_howl_detect_f64* clone() {
		return new icc_howl_detect_f64(*this);
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
			double fTemp0 = static_cast<double>(input0[i0]);
			double fTemp1 = fTemp0 - fConst5 * (fConst6 * fRec2[2] + fConst7 * fRec2[1]);
			fRec2[0] = ((std::fabs(fTemp1) > 2.2250738585072014e-308) ? fTemp1 : 0.0);
			double fTemp2 = std::fabs(fConst4 * (fRec2[0] - fRec2[2]));
			double fTemp3 = ((fTemp2 > fRec1[1]) ? fConst9 : fConst8);
			double fTemp4 = fTemp2 * (1.0 - fTemp3) + fRec1[1] * fTemp3;
			fRec1[0] = ((std::fabs(fTemp4) > 2.2250738585072014e-308) ? fTemp4 : 0.0);
			double fTemp5 = fTemp0 - fConst14 * (fConst15 * fRec4[2] + fConst16 * fRec4[1]);
			fRec4[0] = ((std::fabs(fTemp5) > 2.2250738585072014e-308) ? fTemp5 : 0.0);
			double fTemp6 = std::fabs(fConst13 * (fRec4[0] - fRec4[2]));
			double fTemp7 = ((fTemp6 > fRec3[1]) ? fConst9 : fConst8);
			double fTemp8 = fTemp6 * (1.0 - fTemp7) + fRec3[1] * fTemp7;
			fRec3[0] = ((std::fabs(fTemp8) > 2.2250738585072014e-308) ? fTemp8 : 0.0);
			double fTemp9 = fTemp0 - fConst21 * (fConst22 * fRec6[2] + fConst23 * fRec6[1]);
			fRec6[0] = ((std::fabs(fTemp9) > 2.2250738585072014e-308) ? fTemp9 : 0.0);
			double fTemp10 = std::fabs(fConst20 * (fRec6[0] - fRec6[2]));
			double fTemp11 = ((fTemp10 > fRec5[1]) ? fConst9 : fConst8);
			double fTemp12 = fTemp10 * (1.0 - fTemp11) + fRec5[1] * fTemp11;
			fRec5[0] = ((std::fabs(fTemp12) > 2.2250738585072014e-308) ? fTemp12 : 0.0);
			double fTemp13 = fTemp0 - fConst28 * (fConst29 * fRec8[2] + fConst30 * fRec8[1]);
			fRec8[0] = ((std::fabs(fTemp13) > 2.2250738585072014e-308) ? fTemp13 : 0.0);
			double fTemp14 = std::fabs(fConst27 * (fRec8[0] - fRec8[2]));
			double fTemp15 = ((fTemp14 > fRec7[1]) ? fConst9 : fConst8);
			double fTemp16 = fTemp14 * (1.0 - fTemp15) + fRec7[1] * fTemp15;
			fRec7[0] = ((std::fabs(fTemp16) > 2.2250738585072014e-308) ? fTemp16 : 0.0);
			double fTemp17 = fTemp0 - fConst35 * (fConst36 * fRec10[2] + fConst37 * fRec10[1]);
			fRec10[0] = ((std::fabs(fTemp17) > 2.2250738585072014e-308) ? fTemp17 : 0.0);
			double fTemp18 = std::fabs(fConst34 * (fRec10[0] - fRec10[2]));
			double fTemp19 = ((fTemp18 > fRec9[1]) ? fConst9 : fConst8);
			double fTemp20 = fTemp18 * (1.0 - fTemp19) + fRec9[1] * fTemp19;
			fRec9[0] = ((std::fabs(fTemp20) > 2.2250738585072014e-308) ? fTemp20 : 0.0);
			double fTemp21 = fTemp0 - fConst42 * (fConst43 * fRec12[2] + fConst44 * fRec12[1]);
			fRec12[0] = ((std::fabs(fTemp21) > 2.2250738585072014e-308) ? fTemp21 : 0.0);
			double fTemp22 = std::fabs(fConst41 * (fRec12[0] - fRec12[2]));
			double fTemp23 = ((fTemp22 > fRec11[1]) ? fConst9 : fConst8);
			double fTemp24 = fTemp22 * (1.0 - fTemp23) + fRec11[1] * fTemp23;
			fRec11[0] = ((std::fabs(fTemp24) > 2.2250738585072014e-308) ? fTemp24 : 0.0);
			double fTemp25 = fTemp0 - fConst49 * (fConst50 * fRec14[2] + fConst51 * fRec14[1]);
			fRec14[0] = ((std::fabs(fTemp25) > 2.2250738585072014e-308) ? fTemp25 : 0.0);
			double fTemp26 = std::fabs(fConst48 * (fRec14[0] - fRec14[2]));
			double fTemp27 = ((fTemp26 > fRec13[1]) ? fConst9 : fConst8);
			double fTemp28 = fTemp26 * (1.0 - fTemp27) + fRec13[1] * fTemp27;
			fRec13[0] = ((std::fabs(fTemp28) > 2.2250738585072014e-308) ? fTemp28 : 0.0);
			double fTemp29 = fTemp0 - fConst56 * (fConst57 * fRec16[2] + fConst58 * fRec16[1]);
			fRec16[0] = ((std::fabs(fTemp29) > 2.2250738585072014e-308) ? fTemp29 : 0.0);
			double fTemp30 = std::fabs(fConst55 * (fRec16[0] - fRec16[2]));
			double fTemp31 = ((fTemp30 > fRec15[1]) ? fConst9 : fConst8);
			double fTemp32 = fTemp30 * (1.0 - fTemp31) + fRec15[1] * fTemp31;
			fRec15[0] = ((std::fabs(fTemp32) > 2.2250738585072014e-308) ? fTemp32 : 0.0);
			double fTemp33 = fTemp0 - fConst63 * (fConst64 * fRec18[2] + fConst65 * fRec18[1]);
			fRec18[0] = ((std::fabs(fTemp33) > 2.2250738585072014e-308) ? fTemp33 : 0.0);
			double fTemp34 = std::fabs(fConst62 * (fRec18[0] - fRec18[2]));
			double fTemp35 = ((fTemp34 > fRec17[1]) ? fConst9 : fConst8);
			double fTemp36 = fTemp34 * (1.0 - fTemp35) + fRec17[1] * fTemp35;
			fRec17[0] = ((std::fabs(fTemp36) > 2.2250738585072014e-308) ? fTemp36 : 0.0);
			double fTemp37 = fTemp0 - fConst70 * (fConst71 * fRec20[2] + fConst72 * fRec20[1]);
			fRec20[0] = ((std::fabs(fTemp37) > 2.2250738585072014e-308) ? fTemp37 : 0.0);
			double fTemp38 = std::fabs(fConst69 * (fRec20[0] - fRec20[2]));
			double fTemp39 = ((fTemp38 > fRec19[1]) ? fConst9 : fConst8);
			double fTemp40 = fTemp38 * (1.0 - fTemp39) + fRec19[1] * fTemp39;
			fRec19[0] = ((std::fabs(fTemp40) > 2.2250738585072014e-308) ? fTemp40 : 0.0);
			double fTemp41 = fTemp0 - fConst77 * (fConst78 * fRec22[2] + fConst79 * fRec22[1]);
			fRec22[0] = ((std::fabs(fTemp41) > 2.2250738585072014e-308) ? fTemp41 : 0.0);
			double fTemp42 = std::fabs(fConst76 * (fRec22[0] - fRec22[2]));
			double fTemp43 = ((fTemp42 > fRec21[1]) ? fConst9 : fConst8);
			double fTemp44 = fTemp42 * (1.0 - fTemp43) + fRec21[1] * fTemp43;
			fRec21[0] = ((std::fabs(fTemp44) > 2.2250738585072014e-308) ? fTemp44 : 0.0);
			double fTemp45 = fTemp0 - fConst84 * (fConst85 * fRec24[2] + fConst86 * fRec24[1]);
			fRec24[0] = ((std::fabs(fTemp45) > 2.2250738585072014e-308) ? fTemp45 : 0.0);
			double fTemp46 = std::fabs(fConst83 * (fRec24[0] - fRec24[2]));
			double fTemp47 = ((fTemp46 > fRec23[1]) ? fConst9 : fConst8);
			double fTemp48 = fTemp46 * (1.0 - fTemp47) + fRec23[1] * fTemp47;
			fRec23[0] = ((std::fabs(fTemp48) > 2.2250738585072014e-308) ? fTemp48 : 0.0);
			double fTemp49 = fTemp0 - fConst91 * (fConst92 * fRec26[2] + fConst93 * fRec26[1]);
			fRec26[0] = ((std::fabs(fTemp49) > 2.2250738585072014e-308) ? fTemp49 : 0.0);
			double fTemp50 = std::fabs(fConst90 * (fRec26[0] - fRec26[2]));
			double fTemp51 = ((fTemp50 > fRec25[1]) ? fConst9 : fConst8);
			double fTemp52 = fTemp50 * (1.0 - fTemp51) + fRec25[1] * fTemp51;
			fRec25[0] = ((std::fabs(fTemp52) > 2.2250738585072014e-308) ? fTemp52 : 0.0);
			double fTemp53 = fTemp0 - fConst98 * (fConst99 * fRec28[2] + fConst100 * fRec28[1]);
			fRec28[0] = ((std::fabs(fTemp53) > 2.2250738585072014e-308) ? fTemp53 : 0.0);
			double fTemp54 = std::fabs(fConst97 * (fRec28[0] - fRec28[2]));
			double fTemp55 = ((fTemp54 > fRec27[1]) ? fConst9 : fConst8);
			double fTemp56 = fTemp54 * (1.0 - fTemp55) + fRec27[1] * fTemp55;
			fRec27[0] = ((std::fabs(fTemp56) > 2.2250738585072014e-308) ? fTemp56 : 0.0);
			double fTemp57 = fTemp0 - fConst105 * (fConst106 * fRec30[2] + fConst107 * fRec30[1]);
			fRec30[0] = ((std::fabs(fTemp57) > 2.2250738585072014e-308) ? fTemp57 : 0.0);
			double fTemp58 = std::fabs(fConst104 * (fRec30[0] - fRec30[2]));
			double fTemp59 = ((fTemp58 > fRec29[1]) ? fConst9 : fConst8);
			double fTemp60 = fTemp58 * (1.0 - fTemp59) + fRec29[1] * fTemp59;
			fRec29[0] = ((std::fabs(fTemp60) > 2.2250738585072014e-308) ? fTemp60 : 0.0);
			double fTemp61 = fTemp0 - fConst112 * (fConst113 * fRec32[2] + fConst114 * fRec32[1]);
			fRec32[0] = ((std::fabs(fTemp61) > 2.2250738585072014e-308) ? fTemp61 : 0.0);
			double fTemp62 = std::fabs(fConst111 * (fRec32[0] - fRec32[2]));
			double fTemp63 = ((fTemp62 > fRec31[1]) ? fConst9 : fConst8);
			double fTemp64 = fTemp62 * (1.0 - fTemp63) + fRec31[1] * fTemp63;
			fRec31[0] = ((std::fabs(fTemp64) > 2.2250738585072014e-308) ? fTemp64 : 0.0);
			double fTemp65 = fTemp0 - fConst119 * (fConst120 * fRec34[2] + fConst121 * fRec34[1]);
			fRec34[0] = ((std::fabs(fTemp65) > 2.2250738585072014e-308) ? fTemp65 : 0.0);
			double fTemp66 = std::fabs(fConst118 * (fRec34[0] - fRec34[2]));
			double fTemp67 = ((fTemp66 > fRec33[1]) ? fConst9 : fConst8);
			double fTemp68 = fTemp66 * (1.0 - fTemp67) + fRec33[1] * fTemp67;
			fRec33[0] = ((std::fabs(fTemp68) > 2.2250738585072014e-308) ? fTemp68 : 0.0);
			double fTemp69 = fTemp0 - fConst126 * (fConst127 * fRec36[2] + fConst128 * fRec36[1]);
			fRec36[0] = ((std::fabs(fTemp69) > 2.2250738585072014e-308) ? fTemp69 : 0.0);
			double fTemp70 = std::fabs(fConst125 * (fRec36[0] - fRec36[2]));
			double fTemp71 = ((fTemp70 > fRec35[1]) ? fConst9 : fConst8);
			double fTemp72 = fTemp70 * (1.0 - fTemp71) + fRec35[1] * fTemp71;
			fRec35[0] = ((std::fabs(fTemp72) > 2.2250738585072014e-308) ? fTemp72 : 0.0);
			double fTemp73 = fTemp0 - fConst133 * (fConst134 * fRec38[2] + fConst135 * fRec38[1]);
			fRec38[0] = ((std::fabs(fTemp73) > 2.2250738585072014e-308) ? fTemp73 : 0.0);
			double fTemp74 = std::fabs(fConst132 * (fRec38[0] - fRec38[2]));
			double fTemp75 = ((fTemp74 > fRec37[1]) ? fConst9 : fConst8);
			double fTemp76 = fTemp74 * (1.0 - fTemp75) + fRec37[1] * fTemp75;
			fRec37[0] = ((std::fabs(fTemp76) > 2.2250738585072014e-308) ? fTemp76 : 0.0);
			double fTemp77 = fTemp0 - fConst140 * (fConst141 * fRec40[2] + fConst142 * fRec40[1]);
			fRec40[0] = ((std::fabs(fTemp77) > 2.2250738585072014e-308) ? fTemp77 : 0.0);
			double fTemp78 = std::fabs(fConst139 * (fRec40[0] - fRec40[2]));
			double fTemp79 = ((fTemp78 > fRec39[1]) ? fConst9 : fConst8);
			double fTemp80 = fTemp78 * (1.0 - fTemp79) + fRec39[1] * fTemp79;
			fRec39[0] = ((std::fabs(fTemp80) > 2.2250738585072014e-308) ? fTemp80 : 0.0);
			double fTemp81 = fTemp0 - fConst147 * (fConst148 * fRec42[2] + fConst149 * fRec42[1]);
			fRec42[0] = ((std::fabs(fTemp81) > 2.2250738585072014e-308) ? fTemp81 : 0.0);
			double fTemp82 = std::fabs(fConst146 * (fRec42[0] - fRec42[2]));
			double fTemp83 = ((fTemp82 > fRec41[1]) ? fConst9 : fConst8);
			double fTemp84 = fTemp82 * (1.0 - fTemp83) + fRec41[1] * fTemp83;
			fRec41[0] = ((std::fabs(fTemp84) > 2.2250738585072014e-308) ? fTemp84 : 0.0);
			double fTemp85 = fTemp0 - fConst154 * (fConst155 * fRec44[2] + fConst156 * fRec44[1]);
			fRec44[0] = ((std::fabs(fTemp85) > 2.2250738585072014e-308) ? fTemp85 : 0.0);
			double fTemp86 = std::fabs(fConst153 * (fRec44[0] - fRec44[2]));
			double fTemp87 = ((fTemp86 > fRec43[1]) ? fConst9 : fConst8);
			double fTemp88 = fTemp86 * (1.0 - fTemp87) + fRec43[1] * fTemp87;
			fRec43[0] = ((std::fabs(fTemp88) > 2.2250738585072014e-308) ? fTemp88 : 0.0);
			double fTemp89 = fTemp0 - fConst161 * (fConst162 * fRec46[2] + fConst163 * fRec46[1]);
			fRec46[0] = ((std::fabs(fTemp89) > 2.2250738585072014e-308) ? fTemp89 : 0.0);
			double fTemp90 = std::fabs(fConst160 * (fRec46[0] - fRec46[2]));
			double fTemp91 = ((fTemp90 > fRec45[1]) ? fConst9 : fConst8);
			double fTemp92 = fTemp90 * (1.0 - fTemp91) + fRec45[1] * fTemp91;
			fRec45[0] = ((std::fabs(fTemp92) > 2.2250738585072014e-308) ? fTemp92 : 0.0);
			double fTemp93 = fTemp0 - fConst168 * (fConst169 * fRec48[2] + fConst170 * fRec48[1]);
			fRec48[0] = ((std::fabs(fTemp93) > 2.2250738585072014e-308) ? fTemp93 : 0.0);
			double fTemp94 = std::fabs(fConst167 * (fRec48[0] - fRec48[2]));
			double fTemp95 = ((fTemp94 > fRec47[1]) ? fConst9 : fConst8);
			double fTemp96 = fTemp94 * (1.0 - fTemp95) + fRec47[1] * fTemp95;
			fRec47[0] = ((std::fabs(fTemp96) > 2.2250738585072014e-308) ? fTemp96 : 0.0);
			double fTemp97 = fTemp0 - fConst175 * (fConst176 * fRec50[2] + fConst177 * fRec50[1]);
			fRec50[0] = ((std::fabs(fTemp97) > 2.2250738585072014e-308) ? fTemp97 : 0.0);
			double fTemp98 = std::fabs(fConst174 * (fRec50[0] - fRec50[2]));
			double fTemp99 = ((fTemp98 > fRec49[1]) ? fConst9 : fConst8);
			double fTemp100 = fTemp98 * (1.0 - fTemp99) + fRec49[1] * fTemp99;
			fRec49[0] = ((std::fabs(fTemp100) > 2.2250738585072014e-308) ? fTemp100 : 0.0);
			double fTemp101 = fTemp0 - fConst182 * (fConst183 * fRec52[2] + fConst184 * fRec52[1]);
			fRec52[0] = ((std::fabs(fTemp101) > 2.2250738585072014e-308) ? fTemp101 : 0.0);
			double fTemp102 = std::fabs(fConst181 * (fRec52[0] - fRec52[2]));
			double fTemp103 = ((fTemp102 > fRec51[1]) ? fConst9 : fConst8);
			double fTemp104 = fTemp102 * (1.0 - fTemp103) + fRec51[1] * fTemp103;
			fRec51[0] = ((std::fabs(fTemp104) > 2.2250738585072014e-308) ? fTemp104 : 0.0);
			double fTemp105 = fTemp0 - fConst189 * (fConst190 * fRec54[2] + fConst191 * fRec54[1]);
			fRec54[0] = ((std::fabs(fTemp105) > 2.2250738585072014e-308) ? fTemp105 : 0.0);
			double fTemp106 = std::fabs(fConst188 * (fRec54[0] - fRec54[2]));
			double fTemp107 = ((fTemp106 > fRec53[1]) ? fConst9 : fConst8);
			double fTemp108 = fTemp106 * (1.0 - fTemp107) + fRec53[1] * fTemp107;
			fRec53[0] = ((std::fabs(fTemp108) > 2.2250738585072014e-308) ? fTemp108 : 0.0);
			double fTemp109 = fTemp0 - fConst196 * (fConst197 * fRec56[2] + fConst198 * fRec56[1]);
			fRec56[0] = ((std::fabs(fTemp109) > 2.2250738585072014e-308) ? fTemp109 : 0.0);
			double fTemp110 = std::fabs(fConst195 * (fRec56[0] - fRec56[2]));
			double fTemp111 = ((fTemp110 > fRec55[1]) ? fConst9 : fConst8);
			double fTemp112 = fTemp110 * (1.0 - fTemp111) + fRec55[1] * fTemp111;
			fRec55[0] = ((std::fabs(fTemp112) > 2.2250738585072014e-308) ? fTemp112 : 0.0);
			double fTemp113 = fTemp0 - fConst203 * (fConst204 * fRec58[2] + fConst205 * fRec58[1]);
			fRec58[0] = ((std::fabs(fTemp113) > 2.2250738585072014e-308) ? fTemp113 : 0.0);
			double fTemp114 = std::fabs(fConst202 * (fRec58[0] - fRec58[2]));
			double fTemp115 = ((fTemp114 > fRec57[1]) ? fConst9 : fConst8);
			double fTemp116 = fTemp114 * (1.0 - fTemp115) + fRec57[1] * fTemp115;
			fRec57[0] = ((std::fabs(fTemp116) > 2.2250738585072014e-308) ? fTemp116 : 0.0);
			double fTemp117 = fTemp0 - fConst210 * (fConst211 * fRec60[2] + fConst212 * fRec60[1]);
			fRec60[0] = ((std::fabs(fTemp117) > 2.2250738585072014e-308) ? fTemp117 : 0.0);
			double fTemp118 = std::fabs(fConst209 * (fRec60[0] - fRec60[2]));
			double fTemp119 = ((fTemp118 > fRec59[1]) ? fConst9 : fConst8);
			double fTemp120 = fTemp118 * (1.0 - fTemp119) + fRec59[1] * fTemp119;
			fRec59[0] = ((std::fabs(fTemp120) > 2.2250738585072014e-308) ? fTemp120 : 0.0);
			double fTemp121 = fTemp0 - fConst217 * (fConst218 * fRec62[2] + fConst219 * fRec62[1]);
			fRec62[0] = ((std::fabs(fTemp121) > 2.2250738585072014e-308) ? fTemp121 : 0.0);
			double fTemp122 = std::fabs(fConst216 * (fRec62[0] - fRec62[2]));
			double fTemp123 = ((fTemp122 > fRec61[1]) ? fConst9 : fConst8);
			double fTemp124 = fTemp122 * (1.0 - fTemp123) + fRec61[1] * fTemp123;
			fRec61[0] = ((std::fabs(fTemp124) > 2.2250738585072014e-308) ? fTemp124 : 0.0);
			double fTemp125 = fTemp0 - fConst224 * (fConst225 * fRec64[2] + fConst226 * fRec64[1]);
			fRec64[0] = ((std::fabs(fTemp125) > 2.2250738585072014e-308) ? fTemp125 : 0.0);
			double fTemp126 = std::fabs(fConst223 * (fRec64[0] - fRec64[2]));
			double fTemp127 = ((fTemp126 > fRec63[1]) ? fConst9 : fConst8);
			double fTemp128 = fTemp126 * (1.0 - fTemp127) + fRec63[1] * fTemp127;
			fRec63[0] = ((std::fabs(fTemp128) > 2.2250738585072014e-308) ? fTemp128 : 0.0);
			int iTemp129 = fRec61[0] > fRec63[0];
			double fTemp130 = ((iTemp129) ? fRec61[0] : fRec63[0]);
			int iTemp131 = fRec59[0] > fTemp130;
			double fTemp132 = ((iTemp131) ? fRec59[0] : fTemp130);
			int iTemp133 = fRec57[0] > fTemp132;
			double fTemp134 = ((iTemp133) ? fRec57[0] : fTemp132);
			int iTemp135 = fRec55[0] > fTemp134;
			double fTemp136 = ((iTemp135) ? fRec55[0] : fTemp134);
			int iTemp137 = fRec53[0] > fTemp136;
			double fTemp138 = ((iTemp137) ? fRec53[0] : fTemp136);
			int iTemp139 = fRec51[0] > fTemp138;
			double fTemp140 = ((iTemp139) ? fRec51[0] : fTemp138);
			int iTemp141 = fRec49[0] > fTemp140;
			double fTemp142 = ((iTemp141) ? fRec49[0] : fTemp140);
			int iTemp143 = fRec47[0] > fTemp142;
			double fTemp144 = ((iTemp143) ? fRec47[0] : fTemp142);
			int iTemp145 = fRec45[0] > fTemp144;
			double fTemp146 = ((iTemp145) ? fRec45[0] : fTemp144);
			int iTemp147 = fRec43[0] > fTemp146;
			double fTemp148 = ((iTemp147) ? fRec43[0] : fTemp146);
			int iTemp149 = fRec41[0] > fTemp148;
			double fTemp150 = ((iTemp149) ? fRec41[0] : fTemp148);
			int iTemp151 = fRec39[0] > fTemp150;
			double fTemp152 = ((iTemp151) ? fRec39[0] : fTemp150);
			int iTemp153 = fRec37[0] > fTemp152;
			double fTemp154 = ((iTemp153) ? fRec37[0] : fTemp152);
			int iTemp155 = fRec35[0] > fTemp154;
			double fTemp156 = ((iTemp155) ? fRec35[0] : fTemp154);
			int iTemp157 = fRec33[0] > fTemp156;
			double fTemp158 = ((iTemp157) ? fRec33[0] : fTemp156);
			int iTemp159 = fRec31[0] > fTemp158;
			double fTemp160 = ((iTemp159) ? fRec31[0] : fTemp158);
			int iTemp161 = fRec29[0] > fTemp160;
			double fTemp162 = ((iTemp161) ? fRec29[0] : fTemp160);
			int iTemp163 = fRec27[0] > fTemp162;
			double fTemp164 = ((iTemp163) ? fRec27[0] : fTemp162);
			int iTemp165 = fRec25[0] > fTemp164;
			double fTemp166 = ((iTemp165) ? fRec25[0] : fTemp164);
			int iTemp167 = fRec23[0] > fTemp166;
			double fTemp168 = ((iTemp167) ? fRec23[0] : fTemp166);
			int iTemp169 = fRec21[0] > fTemp168;
			double fTemp170 = ((iTemp169) ? fRec21[0] : fTemp168);
			int iTemp171 = fRec19[0] > fTemp170;
			double fTemp172 = ((iTemp171) ? fRec19[0] : fTemp170);
			int iTemp173 = fRec17[0] > fTemp172;
			double fTemp174 = ((iTemp173) ? fRec17[0] : fTemp172);
			int iTemp175 = fRec15[0] > fTemp174;
			double fTemp176 = ((iTemp175) ? fRec15[0] : fTemp174);
			int iTemp177 = fRec13[0] > fTemp176;
			double fTemp178 = ((iTemp177) ? fRec13[0] : fTemp176);
			int iTemp179 = fRec11[0] > fTemp178;
			double fTemp180 = ((iTemp179) ? fRec11[0] : fTemp178);
			int iTemp181 = fRec9[0] > fTemp180;
			double fTemp182 = ((iTemp181) ? fRec9[0] : fTemp180);
			int iTemp183 = fRec7[0] > fTemp182;
			double fTemp184 = ((iTemp183) ? fRec7[0] : fTemp182);
			int iTemp185 = fRec5[0] > fTemp184;
			double fTemp186 = ((iTemp185) ? fRec5[0] : fTemp184);
			int iTemp187 = fRec3[0] > fTemp186;
			double fTemp188 = ((iTemp187) ? fRec3[0] : fTemp186);
			int iTemp189 = fRec1[0] > fTemp188;
			double fTemp190 = 2e+01 * std::log10((1e-09 + ((iTemp189) ? fRec1[0] : fTemp188)) / (1e-09 + 0.03125 * (fRec63[0] + fRec61[0] + fRec59[0] + fRec57[0] + fRec55[0] + fRec53[0] + fRec51[0] + fRec49[0] + fRec47[0] + fRec45[0] + fRec43[0] + fRec41[0] + fRec39[0] + fRec37[0] + fRec35[0] + fRec33[0] + fRec31[0] + fRec29[0] + fRec27[0] + fRec25[0] + fRec23[0] + fRec21[0] + fRec19[0] + fRec17[0] + fRec15[0] + fRec13[0] + fRec11[0] + fRec9[0] + fRec7[0] + fRec5[0] + fRec1[0] + fRec3[0])));
			double fTemp191 = static_cast<double>(std::abs(fTemp190 > 15.0));
			double fTemp192 = ((fTemp191 > fRec0[1]) ? fConst228 : fConst227);
			double fTemp193 = fTemp191 * (1.0 - fTemp192) + fRec0[1] * fTemp192;
			fRec0[0] = ((std::fabs(fTemp193) > 2.2250738585072014e-308) ? fTemp193 : 0.0);
			output0[i0] = static_cast<FAUSTFLOAT>(fRec0[0]);
			output1[i0] = static_cast<FAUSTFLOAT>(((iTemp189) ? 6e+03 : ((iTemp187) ? 5326.86751846578 : ((iTemp185) ? 4729.252926547629 : ((iTemp183) ? 4198.6839668392995 : ((iTemp181) ? 3727.638873876553 : ((iTemp179) ? 3309.4397396372287 : ((iTemp177) ? 2938.1578422322327 : ((iTemp175) ? 2608.529595652064 : ((iTemp173) ? 2315.8819290059428 : ((iTemp171) ? 2056.0660373706055 : ((iTemp169) ? 1825.398565048354 : ((iTemp167) ? 1620.609387401687 : ((iTemp165) ? 1438.7952509784623 : ((iTemp163) ? 1277.3786146933319 : ((iTemp161) ? 1134.0711085654536 : ((iTemp159) ? 1006.8410919746325 : ((iTemp157) ? 893.884851516048 : ((iTemp155) ? 793.6010301315737 : ((iTemp153) ? 704.5679250048105 : ((iTemp151) ? 625.5233323768265 : ((iTemp149) ? 555.3466535467651 : ((iTemp147) ? 493.043008377822 : ((iTemp145) ? 437.7291310890786 : ((iTemp143) ? 388.620848380777 : ((iTemp141) ? 345.02196237302934 : ((iTemp139) ? 306.3143807537021 : ((iTemp137) ? 271.9493542126425 : ((iTemp135) ? 241.4396969371784 : ((iTemp133) ? 214.3528798804796 : ((iTemp131) ? 190.3048988874873 : ((iTemp129) ? 168.95483074811176 : 1.5e+02))))))))))))))))))))))))))))))));
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
