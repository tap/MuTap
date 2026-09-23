/* ------------------------------------------------------------
name: "icc_suppressor"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn icc_suppressor_f32 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 1
------------------------------------------------------------ */

#ifndef  __icc_suppressor_f32_H__
#define  __icc_suppressor_f32_H__

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
#define FAUSTCLASS icc_suppressor_f32
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

class icc_suppressor_f32SIG0 {
	
  private:
	
	int iVec1[2];
	int iRec214[2];
	int fSampleRate;
	
  public:
	
	int getNumInputsicc_suppressor_f32SIG0() {
		return 0;
	}
	int getNumOutputsicc_suppressor_f32SIG0() {
		return 1;
	}
	
	void instanceIniticc_suppressor_f32SIG0(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l215 = 0; l215 < 2; l215 = faust_wrap_add(l215, 1)) {
			iVec1[l215] = 0;
		}
		for (int l216 = 0; l216 < 2; l216 = faust_wrap_add(l216, 1)) {
			iRec214[l216] = 0;
		}
	}
	
	void fillicc_suppressor_f32SIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = faust_wrap_add(i1, 1)) {
			iVec1[0] = 1;
			iRec214[0] = (faust_wrap_add(iVec1[1], iRec214[1])) % 65536;
			table[i1] = std::cos(9.58738e-05f * static_cast<float>(iRec214[0]));
			iVec1[1] = iVec1[0];
			iRec214[1] = iRec214[0];
		}
	}

};

static icc_suppressor_f32SIG0* newicc_suppressor_f32SIG0() { return (icc_suppressor_f32SIG0*)new icc_suppressor_f32SIG0(); }
static void deleteicc_suppressor_f32SIG0(icc_suppressor_f32SIG0* dsp) { delete dsp; }

class icc_suppressor_f32SIG1 {
	
  private:
	
	int iVec2[2];
	int iRec216[2];
	int fSampleRate;
	
  public:
	
	int getNumInputsicc_suppressor_f32SIG1() {
		return 0;
	}
	int getNumOutputsicc_suppressor_f32SIG1() {
		return 1;
	}
	
	void instanceIniticc_suppressor_f32SIG1(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l218 = 0; l218 < 2; l218 = faust_wrap_add(l218, 1)) {
			iVec2[l218] = 0;
		}
		for (int l219 = 0; l219 < 2; l219 = faust_wrap_add(l219, 1)) {
			iRec216[l219] = 0;
		}
	}
	
	void fillicc_suppressor_f32SIG1(int count, float* table) {
		for (int i2 = 0; i2 < count; i2 = faust_wrap_add(i2, 1)) {
			iVec2[0] = 1;
			iRec216[0] = (faust_wrap_add(iVec2[1], iRec216[1])) % 65536;
			table[i2] = std::sin(9.58738e-05f * static_cast<float>(iRec216[0]));
			iVec2[1] = iVec2[0];
			iRec216[1] = iRec216[0];
		}
	}

};

static icc_suppressor_f32SIG1* newicc_suppressor_f32SIG1() { return (icc_suppressor_f32SIG1*)new icc_suppressor_f32SIG1(); }
static void deleteicc_suppressor_f32SIG1(icc_suppressor_f32SIG1* dsp) { delete dsp; }

static float icc_suppressor_f32_faustpower2_f(float value) {
	return value * value;
}
static float ftbl0icc_suppressor_f32SIG0[65536];
static float ftbl1icc_suppressor_f32SIG1[65536];

class icc_suppressor_f32 : public dsp {
	
 private:
	
	int fSampleRate;
	float fConst0;
	float fConst1;
	float fConst2;
	FAUSTFLOAT fEntry0;
	int iVec0[2];
	int iRec0[3];
	FAUSTFLOAT fEntry1;
	float fConst3;
	float fConst4;
	float fConst5;
	float fConst6;
	float fConst7;
	float fConst8;
	float fConst9;
	float fConst10;
	float fConst11;
	float fConst12;
	float fConst13;
	float fConst14;
	float fConst15;
	float fRec10[3];
	float fConst16;
	float fConst17;
	float fRec9[2];
	float fConst18;
	float fConst19;
	float fConst20;
	float fConst21;
	float fConst22;
	float fConst23;
	float fConst24;
	float fRec12[3];
	float fRec11[2];
	float fConst25;
	float fConst26;
	float fConst27;
	float fConst28;
	float fConst29;
	float fConst30;
	float fConst31;
	float fRec14[3];
	float fRec13[2];
	float fConst32;
	float fConst33;
	float fConst34;
	float fConst35;
	float fConst36;
	float fConst37;
	float fConst38;
	float fRec16[3];
	float fRec15[2];
	float fConst39;
	float fConst40;
	float fConst41;
	float fConst42;
	float fConst43;
	float fConst44;
	float fConst45;
	float fRec18[3];
	float fRec17[2];
	float fConst46;
	float fConst47;
	float fConst48;
	float fConst49;
	float fConst50;
	float fConst51;
	float fConst52;
	float fRec20[3];
	float fRec19[2];
	float fConst53;
	float fConst54;
	float fConst55;
	float fConst56;
	float fConst57;
	float fConst58;
	float fConst59;
	float fRec22[3];
	float fRec21[2];
	float fConst60;
	float fConst61;
	float fConst62;
	float fConst63;
	float fConst64;
	float fConst65;
	float fConst66;
	float fRec24[3];
	float fRec23[2];
	float fConst67;
	float fConst68;
	float fConst69;
	float fConst70;
	float fConst71;
	float fConst72;
	float fConst73;
	float fRec26[3];
	float fRec25[2];
	float fConst74;
	float fConst75;
	float fConst76;
	float fConst77;
	float fConst78;
	float fConst79;
	float fConst80;
	float fRec28[3];
	float fRec27[2];
	float fConst81;
	float fConst82;
	float fConst83;
	float fConst84;
	float fConst85;
	float fConst86;
	float fConst87;
	float fRec30[3];
	float fRec29[2];
	float fConst88;
	float fConst89;
	float fConst90;
	float fConst91;
	float fConst92;
	float fConst93;
	float fConst94;
	float fRec32[3];
	float fRec31[2];
	float fConst95;
	float fConst96;
	float fConst97;
	float fConst98;
	float fConst99;
	float fConst100;
	float fConst101;
	float fRec34[3];
	float fRec33[2];
	float fConst102;
	float fConst103;
	float fConst104;
	float fConst105;
	float fConst106;
	float fConst107;
	float fConst108;
	float fRec36[3];
	float fRec35[2];
	float fConst109;
	float fConst110;
	float fConst111;
	float fConst112;
	float fConst113;
	float fConst114;
	float fConst115;
	float fRec38[3];
	float fRec37[2];
	float fConst116;
	float fConst117;
	float fConst118;
	float fConst119;
	float fConst120;
	float fConst121;
	float fConst122;
	float fRec40[3];
	float fRec39[2];
	float fConst123;
	float fConst124;
	float fConst125;
	float fConst126;
	float fConst127;
	float fConst128;
	float fConst129;
	float fRec42[3];
	float fRec41[2];
	float fConst130;
	float fConst131;
	float fConst132;
	float fConst133;
	float fConst134;
	float fConst135;
	float fConst136;
	float fRec44[3];
	float fRec43[2];
	float fConst137;
	float fConst138;
	float fConst139;
	float fConst140;
	float fConst141;
	float fConst142;
	float fConst143;
	float fRec46[3];
	float fRec45[2];
	float fConst144;
	float fConst145;
	float fConst146;
	float fConst147;
	float fConst148;
	float fConst149;
	float fConst150;
	float fRec48[3];
	float fRec47[2];
	float fConst151;
	float fConst152;
	float fConst153;
	float fConst154;
	float fConst155;
	float fConst156;
	float fConst157;
	float fRec50[3];
	float fRec49[2];
	float fConst158;
	float fConst159;
	float fConst160;
	float fConst161;
	float fConst162;
	float fConst163;
	float fConst164;
	float fRec52[3];
	float fRec51[2];
	float fConst165;
	float fConst166;
	float fConst167;
	float fConst168;
	float fConst169;
	float fConst170;
	float fConst171;
	float fRec54[3];
	float fRec53[2];
	float fConst172;
	float fConst173;
	float fConst174;
	float fConst175;
	float fConst176;
	float fConst177;
	float fConst178;
	float fRec56[3];
	float fRec55[2];
	float fConst179;
	float fConst180;
	float fConst181;
	float fConst182;
	float fConst183;
	float fConst184;
	float fConst185;
	float fRec58[3];
	float fRec57[2];
	float fConst186;
	float fConst187;
	float fConst188;
	float fConst189;
	float fConst190;
	float fConst191;
	float fConst192;
	float fRec60[3];
	float fRec59[2];
	float fConst193;
	float fConst194;
	float fConst195;
	float fConst196;
	float fConst197;
	float fConst198;
	float fConst199;
	float fRec62[3];
	float fRec61[2];
	float fConst200;
	float fConst201;
	float fConst202;
	float fConst203;
	float fConst204;
	float fConst205;
	float fConst206;
	float fRec64[3];
	float fRec63[2];
	float fConst207;
	float fConst208;
	float fConst209;
	float fConst210;
	float fConst211;
	float fConst212;
	float fConst213;
	float fRec66[3];
	float fRec65[2];
	float fConst214;
	float fConst215;
	float fConst216;
	float fConst217;
	float fConst218;
	float fConst219;
	float fConst220;
	float fRec68[3];
	float fRec67[2];
	float fConst221;
	float fConst222;
	float fConst223;
	float fConst224;
	float fConst225;
	float fConst226;
	float fConst227;
	float fRec70[3];
	float fRec69[2];
	float fConst228;
	float fConst229;
	float fConst230;
	float fConst231;
	float fConst232;
	float fConst233;
	float fConst234;
	float fRec72[3];
	float fRec71[2];
	FAUSTFLOAT fEntry2;
	FAUSTFLOAT fEntry3;
	float fConst235;
	float fRec8[2];
	float fRec7[2];
	float fRec6[2];
	float fRec5[3];
	float fConst236;
	float fConst237;
	FAUSTFLOAT fEntry4;
	float fRec73[2];
	float fRec77[3];
	float fRec76[2];
	float fRec79[3];
	float fRec78[2];
	float fRec81[3];
	float fRec80[2];
	float fRec83[3];
	float fRec82[2];
	float fRec85[3];
	float fRec84[2];
	float fRec87[3];
	float fRec86[2];
	float fRec89[3];
	float fRec88[2];
	float fRec91[3];
	float fRec90[2];
	float fRec93[3];
	float fRec92[2];
	float fRec95[3];
	float fRec94[2];
	float fRec97[3];
	float fRec96[2];
	float fRec99[3];
	float fRec98[2];
	float fRec101[3];
	float fRec100[2];
	float fRec103[3];
	float fRec102[2];
	float fRec105[3];
	float fRec104[2];
	float fRec107[3];
	float fRec106[2];
	float fRec109[3];
	float fRec108[2];
	float fRec111[3];
	float fRec110[2];
	float fRec113[3];
	float fRec112[2];
	float fRec115[3];
	float fRec114[2];
	float fRec117[3];
	float fRec116[2];
	float fRec119[3];
	float fRec118[2];
	float fRec121[3];
	float fRec120[2];
	float fRec123[3];
	float fRec122[2];
	float fRec125[3];
	float fRec124[2];
	float fRec127[3];
	float fRec126[2];
	float fRec129[3];
	float fRec128[2];
	float fRec131[3];
	float fRec130[2];
	float fRec133[3];
	float fRec132[2];
	float fRec135[3];
	float fRec134[2];
	float fRec137[3];
	float fRec136[2];
	float fRec139[3];
	float fRec138[2];
	float fRec75[2];
	float fRec74[2];
	float fRec142[2];
	float fRec141[2];
	float fRec140[3];
	float fRec147[3];
	float fRec146[2];
	float fRec149[3];
	float fRec148[2];
	float fRec151[3];
	float fRec150[2];
	float fRec153[3];
	float fRec152[2];
	float fRec155[3];
	float fRec154[2];
	float fRec157[3];
	float fRec156[2];
	float fRec159[3];
	float fRec158[2];
	float fRec161[3];
	float fRec160[2];
	float fRec163[3];
	float fRec162[2];
	float fRec165[3];
	float fRec164[2];
	float fRec167[3];
	float fRec166[2];
	float fRec169[3];
	float fRec168[2];
	float fRec171[3];
	float fRec170[2];
	float fRec173[3];
	float fRec172[2];
	float fRec175[3];
	float fRec174[2];
	float fRec177[3];
	float fRec176[2];
	float fRec179[3];
	float fRec178[2];
	float fRec181[3];
	float fRec180[2];
	float fRec183[3];
	float fRec182[2];
	float fRec185[3];
	float fRec184[2];
	float fRec187[3];
	float fRec186[2];
	float fRec189[3];
	float fRec188[2];
	float fRec191[3];
	float fRec190[2];
	float fRec193[3];
	float fRec192[2];
	float fRec195[3];
	float fRec194[2];
	float fRec197[3];
	float fRec196[2];
	float fRec199[3];
	float fRec198[2];
	float fRec201[3];
	float fRec200[2];
	float fRec203[3];
	float fRec202[2];
	float fRec205[3];
	float fRec204[2];
	float fRec207[3];
	float fRec206[2];
	float fRec209[3];
	float fRec208[2];
	float fRec145[2];
	float fRec144[2];
	float fRec143[2];
	float fRec4[3];
	float fRec210[2];
	float fRec3[3];
	float fRec2[3];
	float fRec1[3];
	float fRec213[3];
	float fRec212[3];
	float fRec211[3];
	FAUSTFLOAT fEntry5;
	float fRec215[2];
	
 public:
	icc_suppressor_f32() {
	}
	
	icc_suppressor_f32(const icc_suppressor_f32&) = default;
	
	virtual ~icc_suppressor_f32() = default;
	
	icc_suppressor_f32& operator=(const icc_suppressor_f32&) = default;
	
	void metadata(Meta* m) { 
		m->declare("analyzers.lib/amp_follower_ar:author", "Jonatan Liljedahl, revised by Romain Michon");
		m->declare("analyzers.lib/name", "Faust Analyzer Library");
		m->declare("analyzers.lib/version", "1.4.0");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/sAndH:author", "Romain Michon");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn icc_suppressor_f32 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 1");
		m->declare("filename", "icc_suppressor.dsp");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/lowpass0_highpass1:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass0_highpass1:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowpass0_highpass1:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/lowpass:author", "Julius O. Smith III");
		m->declare("filters.lib/lowpass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/lowpass:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/notchw:author", "Julius O. Smith III");
		m->declare("filters.lib/notchw:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/notchw:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/pospass:author", "Julius O. Smith III");
		m->declare("filters.lib/pospass:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/pospass:license", "LicenseRef-STK-4.3");
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
		m->declare("name", "icc_suppressor");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/version", "1.8.0");
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
		return 1;
	}
	
	static void classInit(int sample_rate) {
		icc_suppressor_f32SIG0* sig0 = newicc_suppressor_f32SIG0();
		sig0->instanceIniticc_suppressor_f32SIG0(sample_rate);
		sig0->fillicc_suppressor_f32SIG0(65536, ftbl0icc_suppressor_f32SIG0);
		icc_suppressor_f32SIG1* sig1 = newicc_suppressor_f32SIG1();
		sig1->instanceIniticc_suppressor_f32SIG1(sample_rate);
		sig1->fillicc_suppressor_f32SIG1(65536, ftbl1icc_suppressor_f32SIG1);
		deleteicc_suppressor_f32SIG0(sig0);
		deleteicc_suppressor_f32SIG1(sig1);
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		fConst1 = 3.1415927f / fConst0;
		fConst2 = 0.25f * fConst0;
		fConst3 = 94.24778f / fConst0;
		fConst4 = icc_suppressor_f32_faustpower2_f(1.0f - fConst3) / icc_suppressor_f32_faustpower2_f(fConst3 + 1.0f);
		fConst5 = fConst4 + 1.0f;
		fConst6 = 6.2831855f / fConst0;
		fConst7 = std::exp(-(16.008005f / fConst0));
		fConst8 = 1.0f - fConst7;
		fConst9 = std::tan(18849.557f / fConst0);
		fConst10 = 1.0f / fConst9;
		fConst11 = (fConst10 + 0.071428575f) / fConst9 + 1.0f;
		fConst12 = 1.0f / (fConst9 * fConst11);
		fConst13 = 1.0f / fConst11;
		fConst14 = (fConst10 + -0.071428575f) / fConst9 + 1.0f;
		fConst15 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst9));
		fConst16 = std::exp(-(2e+01f / fConst0));
		fConst17 = std::exp(-(2e+02f / fConst0));
		fConst18 = std::tan(16734.848f / fConst0);
		fConst19 = 1.0f / fConst18;
		fConst20 = (fConst19 + 0.071428575f) / fConst18 + 1.0f;
		fConst21 = 1.0f / (fConst18 * fConst20);
		fConst22 = 1.0f / fConst20;
		fConst23 = (fConst19 + -0.071428575f) / fConst18 + 1.0f;
		fConst24 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst18));
		fConst25 = std::tan(14857.387f / fConst0);
		fConst26 = 1.0f / fConst25;
		fConst27 = (fConst26 + 0.071428575f) / fConst25 + 1.0f;
		fConst28 = 1.0f / (fConst25 * fConst27);
		fConst29 = 1.0f / fConst27;
		fConst30 = (fConst26 + -0.071428575f) / fConst25 + 1.0f;
		fConst31 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst25));
		fConst32 = std::tan(13190.555f / fConst0);
		fConst33 = 1.0f / fConst32;
		fConst34 = (fConst33 + 0.071428575f) / fConst32 + 1.0f;
		fConst35 = 1.0f / (fConst32 * fConst34);
		fConst36 = 1.0f / fConst34;
		fConst37 = (fConst33 + -0.071428575f) / fConst32 + 1.0f;
		fConst38 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst32));
		fConst39 = std::tan(11710.723f / fConst0);
		fConst40 = 1.0f / fConst39;
		fConst41 = (fConst40 + 0.071428575f) / fConst39 + 1.0f;
		fConst42 = 1.0f / (fConst39 * fConst41);
		fConst43 = 1.0f / fConst41;
		fConst44 = (fConst40 + -0.071428575f) / fConst39 + 1.0f;
		fConst45 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst39));
		fConst46 = std::tan(10396.911f / fConst0);
		fConst47 = 1.0f / fConst46;
		fConst48 = (fConst47 + 0.071428575f) / fConst46 + 1.0f;
		fConst49 = 1.0f / (fConst46 * fConst48);
		fConst50 = 1.0f / fConst48;
		fConst51 = (fConst47 + -0.071428575f) / fConst46 + 1.0f;
		fConst52 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst46));
		fConst53 = std::tan(9230.495f / fConst0);
		fConst54 = 1.0f / fConst53;
		fConst55 = (fConst54 + 0.071428575f) / fConst53 + 1.0f;
		fConst56 = 1.0f / (fConst53 * fConst55);
		fConst57 = 1.0f / fConst55;
		fConst58 = (fConst54 + -0.071428575f) / fConst53 + 1.0f;
		fConst59 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst53));
		fConst60 = std::tan(8194.9375f / fConst0);
		fConst61 = 1.0f / fConst60;
		fConst62 = (fConst61 + 0.071428575f) / fConst60 + 1.0f;
		fConst63 = 1.0f / (fConst60 * fConst62);
		fConst64 = 1.0f / fConst62;
		fConst65 = (fConst61 + -0.071428575f) / fConst60 + 1.0f;
		fConst66 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst60));
		fConst67 = std::tan(7275.5576f / fConst0);
		fConst68 = 1.0f / fConst67;
		fConst69 = (fConst68 + 0.071428575f) / fConst67 + 1.0f;
		fConst70 = 1.0f / (fConst67 * fConst69);
		fConst71 = 1.0f / fConst69;
		fConst72 = (fConst68 + -0.071428575f) / fConst67 + 1.0f;
		fConst73 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst67));
		fConst74 = std::tan(6459.322f / fConst0);
		fConst75 = 1.0f / fConst74;
		fConst76 = (fConst75 + 0.071428575f) / fConst74 + 1.0f;
		fConst77 = 1.0f / (fConst74 * fConst76);
		fConst78 = 1.0f / fConst76;
		fConst79 = (fConst75 + -0.071428575f) / fConst74 + 1.0f;
		fConst80 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst74));
		fConst81 = std::tan(5734.6587f / fConst0);
		fConst82 = 1.0f / fConst81;
		fConst83 = (fConst82 + 0.071428575f) / fConst81 + 1.0f;
		fConst84 = 1.0f / (fConst81 * fConst83);
		fConst85 = 1.0f / fConst83;
		fConst86 = (fConst82 + -0.071428575f) / fConst81 + 1.0f;
		fConst87 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst81));
		fConst88 = std::tan(5091.2944f / fConst0);
		fConst89 = 1.0f / fConst88;
		fConst90 = (fConst89 + 0.071428575f) / fConst88 + 1.0f;
		fConst91 = 1.0f / (fConst88 * fConst90);
		fConst92 = 1.0f / fConst90;
		fConst93 = (fConst89 + -0.071428575f) / fConst88 + 1.0f;
		fConst94 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst88));
		fConst95 = std::tan(4520.1084f / fConst0);
		fConst96 = 1.0f / fConst95;
		fConst97 = (fConst96 + 0.071428575f) / fConst95 + 1.0f;
		fConst98 = 1.0f / (fConst95 * fConst97);
		fConst99 = 1.0f / fConst97;
		fConst100 = (fConst96 + -0.071428575f) / fConst95 + 1.0f;
		fConst101 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst95));
		fConst102 = std::tan(4013.0032f / fConst0);
		fConst103 = 1.0f / fConst102;
		fConst104 = (fConst103 + 0.071428575f) / fConst102 + 1.0f;
		fConst105 = 1.0f / (fConst102 * fConst104);
		fConst106 = 1.0f / fConst104;
		fConst107 = (fConst103 + -0.071428575f) / fConst102 + 1.0f;
		fConst108 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst102));
		fConst109 = std::tan(3562.7896f / fConst0);
		fConst110 = 1.0f / fConst109;
		fConst111 = (fConst110 + 0.071428575f) / fConst109 + 1.0f;
		fConst112 = 1.0f / (fConst109 * fConst111);
		fConst113 = 1.0f / fConst111;
		fConst114 = (fConst110 + -0.071428575f) / fConst109 + 1.0f;
		fConst115 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst109));
		fConst116 = std::tan(3163.0845f / fConst0);
		fConst117 = 1.0f / fConst116;
		fConst118 = (fConst117 + 0.071428575f) / fConst116 + 1.0f;
		fConst119 = 1.0f / (fConst116 * fConst118);
		fConst120 = 1.0f / fConst118;
		fConst121 = (fConst117 + -0.071428575f) / fConst116 + 1.0f;
		fConst122 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst116));
		fConst123 = std::tan(2808.2222f / fConst0);
		fConst124 = 1.0f / fConst123;
		fConst125 = (fConst124 + 0.071428575f) / fConst123 + 1.0f;
		fConst126 = 1.0f / (fConst123 * fConst125);
		fConst127 = 1.0f / fConst125;
		fConst128 = (fConst124 + -0.071428575f) / fConst123 + 1.0f;
		fConst129 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst123));
		fConst130 = std::tan(2493.1711f / fConst0);
		fConst131 = 1.0f / fConst130;
		fConst132 = (fConst131 + 0.071428575f) / fConst130 + 1.0f;
		fConst133 = 1.0f / (fConst130 * fConst132);
		fConst134 = 1.0f / fConst132;
		fConst135 = (fConst131 + -0.071428575f) / fConst130 + 1.0f;
		fConst136 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst130));
		fConst137 = std::tan(2213.4653f / fConst0);
		fConst138 = 1.0f / fConst137;
		fConst139 = (fConst138 + 0.071428575f) / fConst137 + 1.0f;
		fConst140 = 1.0f / (fConst137 * fConst139);
		fConst141 = 1.0f / fConst139;
		fConst142 = (fConst138 + -0.071428575f) / fConst137 + 1.0f;
		fConst143 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst137));
		fConst144 = std::tan(1965.1395f / fConst0);
		fConst145 = 1.0f / fConst144;
		fConst146 = (fConst145 + 0.071428575f) / fConst144 + 1.0f;
		fConst147 = 1.0f / (fConst144 * fConst146);
		fConst148 = 1.0f / fConst146;
		fConst149 = (fConst145 + -0.071428575f) / fConst144 + 1.0f;
		fConst150 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst144));
		fConst151 = std::tan(1744.673f / fConst0);
		fConst152 = 1.0f / fConst151;
		fConst153 = (fConst152 + 0.071428575f) / fConst151 + 1.0f;
		fConst154 = 1.0f / (fConst151 * fConst153);
		fConst155 = 1.0f / fConst153;
		fConst156 = (fConst152 + -0.071428575f) / fConst151 + 1.0f;
		fConst157 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst151));
		fConst158 = std::tan(1548.9403f / fConst0);
		fConst159 = 1.0f / fConst158;
		fConst160 = (fConst159 + 0.071428575f) / fConst158 + 1.0f;
		fConst161 = 1.0f / (fConst158 * fConst160);
		fConst162 = 1.0f / fConst160;
		fConst163 = (fConst159 + -0.071428575f) / fConst158 + 1.0f;
		fConst164 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst158));
		fConst165 = std::tan(1375.1666f / fConst0);
		fConst166 = 1.0f / fConst165;
		fConst167 = (fConst166 + 0.071428575f) / fConst165 + 1.0f;
		fConst168 = 1.0f / (fConst165 * fConst167);
		fConst169 = 1.0f / fConst167;
		fConst170 = (fConst166 + -0.071428575f) / fConst165 + 1.0f;
		fConst171 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst165));
		fConst172 = std::tan(1220.8884f / fConst0);
		fConst173 = 1.0f / fConst172;
		fConst174 = (fConst173 + 0.071428575f) / fConst172 + 1.0f;
		fConst175 = 1.0f / (fConst172 * fConst174);
		fConst176 = 1.0f / fConst174;
		fConst177 = (fConst173 + -0.071428575f) / fConst172 + 1.0f;
		fConst178 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst172));
		fConst179 = std::tan(1083.9185f / fConst0);
		fConst180 = 1.0f / fConst179;
		fConst181 = (fConst180 + 0.071428575f) / fConst179 + 1.0f;
		fConst182 = 1.0f / (fConst179 * fConst181);
		fConst183 = 1.0f / fConst181;
		fConst184 = (fConst180 + -0.071428575f) / fConst179 + 1.0f;
		fConst185 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst179));
		fConst186 = std::tan(962.315f / fConst0);
		fConst187 = 1.0f / fConst186;
		fConst188 = (fConst187 + 0.071428575f) / fConst186 + 1.0f;
		fConst189 = 1.0f / (fConst186 * fConst188);
		fConst190 = 1.0f / fConst188;
		fConst191 = (fConst187 + -0.071428575f) / fConst186 + 1.0f;
		fConst192 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst186));
		fConst193 = std::tan(854.35406f / fConst0);
		fConst194 = 1.0f / fConst193;
		fConst195 = (fConst194 + 0.071428575f) / fConst193 + 1.0f;
		fConst196 = 1.0f / (fConst193 * fConst195);
		fConst197 = 1.0f / fConst195;
		fConst198 = (fConst194 + -0.071428575f) / fConst193 + 1.0f;
		fConst199 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst193));
		fConst200 = std::tan(758.5052f / fConst0);
		fConst201 = 1.0f / fConst200;
		fConst202 = (fConst201 + 0.071428575f) / fConst200 + 1.0f;
		fConst203 = 1.0f / (fConst200 * fConst202);
		fConst204 = 1.0f / fConst202;
		fConst205 = (fConst201 + -0.071428575f) / fConst200 + 1.0f;
		fConst206 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst200));
		fConst207 = std::tan(673.4094f / fConst0);
		fConst208 = 1.0f / fConst207;
		fConst209 = (fConst208 + 0.071428575f) / fConst207 + 1.0f;
		fConst210 = 1.0f / (fConst207 * fConst209);
		fConst211 = 1.0f / fConst209;
		fConst212 = (fConst208 + -0.071428575f) / fConst207 + 1.0f;
		fConst213 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst207));
		fConst214 = std::tan(597.8605f / fConst0);
		fConst215 = 1.0f / fConst214;
		fConst216 = (fConst215 + 0.071428575f) / fConst214 + 1.0f;
		fConst217 = 1.0f / (fConst214 * fConst216);
		fConst218 = 1.0f / fConst216;
		fConst219 = (fConst215 + -0.071428575f) / fConst214 + 1.0f;
		fConst220 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst214));
		fConst221 = std::tan(530.78723f / fConst0);
		fConst222 = 1.0f / fConst221;
		fConst223 = (fConst222 + 0.071428575f) / fConst221 + 1.0f;
		fConst224 = 1.0f / (fConst221 * fConst223);
		fConst225 = 1.0f / fConst223;
		fConst226 = (fConst222 + -0.071428575f) / fConst221 + 1.0f;
		fConst227 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst221));
		fConst228 = std::tan(471.2389f / fConst0);
		fConst229 = 1.0f / fConst228;
		fConst230 = (fConst229 + 0.071428575f) / fConst228 + 1.0f;
		fConst231 = 1.0f / (fConst228 * fConst230);
		fConst232 = 1.0f / fConst230;
		fConst233 = (fConst229 + -0.071428575f) / fConst228 + 1.0f;
		fConst234 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fConst228));
		fConst235 = 1.0f / fConst0;
		fConst236 = std::exp(-(8.002001f / fConst0));
		fConst237 = 1.0f - fConst236;
	}
	
	virtual void instanceResetUserInterface() {
		fEntry0 = static_cast<FAUSTFLOAT>(1e+02f);
		fEntry1 = static_cast<FAUSTFLOAT>(0.0f);
		fEntry2 = static_cast<FAUSTFLOAT>(15.0f);
		fEntry3 = static_cast<FAUSTFLOAT>(0.2f);
		fEntry4 = static_cast<FAUSTFLOAT>(12.0f);
		fEntry5 = static_cast<FAUSTFLOAT>(2.0f);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 3; l1 = faust_wrap_add(l1, 1)) {
			iRec0[l1] = 0;
		}
		for (int l2 = 0; l2 < 3; l2 = faust_wrap_add(l2, 1)) {
			fRec10[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fRec9[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 3; l4 = faust_wrap_add(l4, 1)) {
			fRec12[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec11[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 3; l6 = faust_wrap_add(l6, 1)) {
			fRec14[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = faust_wrap_add(l7, 1)) {
			fRec13[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 3; l8 = faust_wrap_add(l8, 1)) {
			fRec16[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = faust_wrap_add(l9, 1)) {
			fRec15[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 3; l10 = faust_wrap_add(l10, 1)) {
			fRec18[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = faust_wrap_add(l11, 1)) {
			fRec17[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 3; l12 = faust_wrap_add(l12, 1)) {
			fRec20[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 2; l13 = faust_wrap_add(l13, 1)) {
			fRec19[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 3; l14 = faust_wrap_add(l14, 1)) {
			fRec22[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 2; l15 = faust_wrap_add(l15, 1)) {
			fRec21[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 3; l16 = faust_wrap_add(l16, 1)) {
			fRec24[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 2; l17 = faust_wrap_add(l17, 1)) {
			fRec23[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 3; l18 = faust_wrap_add(l18, 1)) {
			fRec26[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2; l19 = faust_wrap_add(l19, 1)) {
			fRec25[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 3; l20 = faust_wrap_add(l20, 1)) {
			fRec28[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 2; l21 = faust_wrap_add(l21, 1)) {
			fRec27[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 3; l22 = faust_wrap_add(l22, 1)) {
			fRec30[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 2; l23 = faust_wrap_add(l23, 1)) {
			fRec29[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 3; l24 = faust_wrap_add(l24, 1)) {
			fRec32[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 2; l25 = faust_wrap_add(l25, 1)) {
			fRec31[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 3; l26 = faust_wrap_add(l26, 1)) {
			fRec34[l26] = 0.0f;
		}
		for (int l27 = 0; l27 < 2; l27 = faust_wrap_add(l27, 1)) {
			fRec33[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 3; l28 = faust_wrap_add(l28, 1)) {
			fRec36[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 2; l29 = faust_wrap_add(l29, 1)) {
			fRec35[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 3; l30 = faust_wrap_add(l30, 1)) {
			fRec38[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 2; l31 = faust_wrap_add(l31, 1)) {
			fRec37[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 3; l32 = faust_wrap_add(l32, 1)) {
			fRec40[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2; l33 = faust_wrap_add(l33, 1)) {
			fRec39[l33] = 0.0f;
		}
		for (int l34 = 0; l34 < 3; l34 = faust_wrap_add(l34, 1)) {
			fRec42[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 2; l35 = faust_wrap_add(l35, 1)) {
			fRec41[l35] = 0.0f;
		}
		for (int l36 = 0; l36 < 3; l36 = faust_wrap_add(l36, 1)) {
			fRec44[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 2; l37 = faust_wrap_add(l37, 1)) {
			fRec43[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 3; l38 = faust_wrap_add(l38, 1)) {
			fRec46[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 2; l39 = faust_wrap_add(l39, 1)) {
			fRec45[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 3; l40 = faust_wrap_add(l40, 1)) {
			fRec48[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 2; l41 = faust_wrap_add(l41, 1)) {
			fRec47[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 3; l42 = faust_wrap_add(l42, 1)) {
			fRec50[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 2; l43 = faust_wrap_add(l43, 1)) {
			fRec49[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 3; l44 = faust_wrap_add(l44, 1)) {
			fRec52[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 2; l45 = faust_wrap_add(l45, 1)) {
			fRec51[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 3; l46 = faust_wrap_add(l46, 1)) {
			fRec54[l46] = 0.0f;
		}
		for (int l47 = 0; l47 < 2; l47 = faust_wrap_add(l47, 1)) {
			fRec53[l47] = 0.0f;
		}
		for (int l48 = 0; l48 < 3; l48 = faust_wrap_add(l48, 1)) {
			fRec56[l48] = 0.0f;
		}
		for (int l49 = 0; l49 < 2; l49 = faust_wrap_add(l49, 1)) {
			fRec55[l49] = 0.0f;
		}
		for (int l50 = 0; l50 < 3; l50 = faust_wrap_add(l50, 1)) {
			fRec58[l50] = 0.0f;
		}
		for (int l51 = 0; l51 < 2; l51 = faust_wrap_add(l51, 1)) {
			fRec57[l51] = 0.0f;
		}
		for (int l52 = 0; l52 < 3; l52 = faust_wrap_add(l52, 1)) {
			fRec60[l52] = 0.0f;
		}
		for (int l53 = 0; l53 < 2; l53 = faust_wrap_add(l53, 1)) {
			fRec59[l53] = 0.0f;
		}
		for (int l54 = 0; l54 < 3; l54 = faust_wrap_add(l54, 1)) {
			fRec62[l54] = 0.0f;
		}
		for (int l55 = 0; l55 < 2; l55 = faust_wrap_add(l55, 1)) {
			fRec61[l55] = 0.0f;
		}
		for (int l56 = 0; l56 < 3; l56 = faust_wrap_add(l56, 1)) {
			fRec64[l56] = 0.0f;
		}
		for (int l57 = 0; l57 < 2; l57 = faust_wrap_add(l57, 1)) {
			fRec63[l57] = 0.0f;
		}
		for (int l58 = 0; l58 < 3; l58 = faust_wrap_add(l58, 1)) {
			fRec66[l58] = 0.0f;
		}
		for (int l59 = 0; l59 < 2; l59 = faust_wrap_add(l59, 1)) {
			fRec65[l59] = 0.0f;
		}
		for (int l60 = 0; l60 < 3; l60 = faust_wrap_add(l60, 1)) {
			fRec68[l60] = 0.0f;
		}
		for (int l61 = 0; l61 < 2; l61 = faust_wrap_add(l61, 1)) {
			fRec67[l61] = 0.0f;
		}
		for (int l62 = 0; l62 < 3; l62 = faust_wrap_add(l62, 1)) {
			fRec70[l62] = 0.0f;
		}
		for (int l63 = 0; l63 < 2; l63 = faust_wrap_add(l63, 1)) {
			fRec69[l63] = 0.0f;
		}
		for (int l64 = 0; l64 < 3; l64 = faust_wrap_add(l64, 1)) {
			fRec72[l64] = 0.0f;
		}
		for (int l65 = 0; l65 < 2; l65 = faust_wrap_add(l65, 1)) {
			fRec71[l65] = 0.0f;
		}
		for (int l66 = 0; l66 < 2; l66 = faust_wrap_add(l66, 1)) {
			fRec8[l66] = 0.0f;
		}
		for (int l67 = 0; l67 < 2; l67 = faust_wrap_add(l67, 1)) {
			fRec7[l67] = 0.0f;
		}
		for (int l68 = 0; l68 < 2; l68 = faust_wrap_add(l68, 1)) {
			fRec6[l68] = 0.0f;
		}
		for (int l69 = 0; l69 < 3; l69 = faust_wrap_add(l69, 1)) {
			fRec5[l69] = 0.0f;
		}
		for (int l70 = 0; l70 < 2; l70 = faust_wrap_add(l70, 1)) {
			fRec73[l70] = 0.0f;
		}
		for (int l71 = 0; l71 < 3; l71 = faust_wrap_add(l71, 1)) {
			fRec77[l71] = 0.0f;
		}
		for (int l72 = 0; l72 < 2; l72 = faust_wrap_add(l72, 1)) {
			fRec76[l72] = 0.0f;
		}
		for (int l73 = 0; l73 < 3; l73 = faust_wrap_add(l73, 1)) {
			fRec79[l73] = 0.0f;
		}
		for (int l74 = 0; l74 < 2; l74 = faust_wrap_add(l74, 1)) {
			fRec78[l74] = 0.0f;
		}
		for (int l75 = 0; l75 < 3; l75 = faust_wrap_add(l75, 1)) {
			fRec81[l75] = 0.0f;
		}
		for (int l76 = 0; l76 < 2; l76 = faust_wrap_add(l76, 1)) {
			fRec80[l76] = 0.0f;
		}
		for (int l77 = 0; l77 < 3; l77 = faust_wrap_add(l77, 1)) {
			fRec83[l77] = 0.0f;
		}
		for (int l78 = 0; l78 < 2; l78 = faust_wrap_add(l78, 1)) {
			fRec82[l78] = 0.0f;
		}
		for (int l79 = 0; l79 < 3; l79 = faust_wrap_add(l79, 1)) {
			fRec85[l79] = 0.0f;
		}
		for (int l80 = 0; l80 < 2; l80 = faust_wrap_add(l80, 1)) {
			fRec84[l80] = 0.0f;
		}
		for (int l81 = 0; l81 < 3; l81 = faust_wrap_add(l81, 1)) {
			fRec87[l81] = 0.0f;
		}
		for (int l82 = 0; l82 < 2; l82 = faust_wrap_add(l82, 1)) {
			fRec86[l82] = 0.0f;
		}
		for (int l83 = 0; l83 < 3; l83 = faust_wrap_add(l83, 1)) {
			fRec89[l83] = 0.0f;
		}
		for (int l84 = 0; l84 < 2; l84 = faust_wrap_add(l84, 1)) {
			fRec88[l84] = 0.0f;
		}
		for (int l85 = 0; l85 < 3; l85 = faust_wrap_add(l85, 1)) {
			fRec91[l85] = 0.0f;
		}
		for (int l86 = 0; l86 < 2; l86 = faust_wrap_add(l86, 1)) {
			fRec90[l86] = 0.0f;
		}
		for (int l87 = 0; l87 < 3; l87 = faust_wrap_add(l87, 1)) {
			fRec93[l87] = 0.0f;
		}
		for (int l88 = 0; l88 < 2; l88 = faust_wrap_add(l88, 1)) {
			fRec92[l88] = 0.0f;
		}
		for (int l89 = 0; l89 < 3; l89 = faust_wrap_add(l89, 1)) {
			fRec95[l89] = 0.0f;
		}
		for (int l90 = 0; l90 < 2; l90 = faust_wrap_add(l90, 1)) {
			fRec94[l90] = 0.0f;
		}
		for (int l91 = 0; l91 < 3; l91 = faust_wrap_add(l91, 1)) {
			fRec97[l91] = 0.0f;
		}
		for (int l92 = 0; l92 < 2; l92 = faust_wrap_add(l92, 1)) {
			fRec96[l92] = 0.0f;
		}
		for (int l93 = 0; l93 < 3; l93 = faust_wrap_add(l93, 1)) {
			fRec99[l93] = 0.0f;
		}
		for (int l94 = 0; l94 < 2; l94 = faust_wrap_add(l94, 1)) {
			fRec98[l94] = 0.0f;
		}
		for (int l95 = 0; l95 < 3; l95 = faust_wrap_add(l95, 1)) {
			fRec101[l95] = 0.0f;
		}
		for (int l96 = 0; l96 < 2; l96 = faust_wrap_add(l96, 1)) {
			fRec100[l96] = 0.0f;
		}
		for (int l97 = 0; l97 < 3; l97 = faust_wrap_add(l97, 1)) {
			fRec103[l97] = 0.0f;
		}
		for (int l98 = 0; l98 < 2; l98 = faust_wrap_add(l98, 1)) {
			fRec102[l98] = 0.0f;
		}
		for (int l99 = 0; l99 < 3; l99 = faust_wrap_add(l99, 1)) {
			fRec105[l99] = 0.0f;
		}
		for (int l100 = 0; l100 < 2; l100 = faust_wrap_add(l100, 1)) {
			fRec104[l100] = 0.0f;
		}
		for (int l101 = 0; l101 < 3; l101 = faust_wrap_add(l101, 1)) {
			fRec107[l101] = 0.0f;
		}
		for (int l102 = 0; l102 < 2; l102 = faust_wrap_add(l102, 1)) {
			fRec106[l102] = 0.0f;
		}
		for (int l103 = 0; l103 < 3; l103 = faust_wrap_add(l103, 1)) {
			fRec109[l103] = 0.0f;
		}
		for (int l104 = 0; l104 < 2; l104 = faust_wrap_add(l104, 1)) {
			fRec108[l104] = 0.0f;
		}
		for (int l105 = 0; l105 < 3; l105 = faust_wrap_add(l105, 1)) {
			fRec111[l105] = 0.0f;
		}
		for (int l106 = 0; l106 < 2; l106 = faust_wrap_add(l106, 1)) {
			fRec110[l106] = 0.0f;
		}
		for (int l107 = 0; l107 < 3; l107 = faust_wrap_add(l107, 1)) {
			fRec113[l107] = 0.0f;
		}
		for (int l108 = 0; l108 < 2; l108 = faust_wrap_add(l108, 1)) {
			fRec112[l108] = 0.0f;
		}
		for (int l109 = 0; l109 < 3; l109 = faust_wrap_add(l109, 1)) {
			fRec115[l109] = 0.0f;
		}
		for (int l110 = 0; l110 < 2; l110 = faust_wrap_add(l110, 1)) {
			fRec114[l110] = 0.0f;
		}
		for (int l111 = 0; l111 < 3; l111 = faust_wrap_add(l111, 1)) {
			fRec117[l111] = 0.0f;
		}
		for (int l112 = 0; l112 < 2; l112 = faust_wrap_add(l112, 1)) {
			fRec116[l112] = 0.0f;
		}
		for (int l113 = 0; l113 < 3; l113 = faust_wrap_add(l113, 1)) {
			fRec119[l113] = 0.0f;
		}
		for (int l114 = 0; l114 < 2; l114 = faust_wrap_add(l114, 1)) {
			fRec118[l114] = 0.0f;
		}
		for (int l115 = 0; l115 < 3; l115 = faust_wrap_add(l115, 1)) {
			fRec121[l115] = 0.0f;
		}
		for (int l116 = 0; l116 < 2; l116 = faust_wrap_add(l116, 1)) {
			fRec120[l116] = 0.0f;
		}
		for (int l117 = 0; l117 < 3; l117 = faust_wrap_add(l117, 1)) {
			fRec123[l117] = 0.0f;
		}
		for (int l118 = 0; l118 < 2; l118 = faust_wrap_add(l118, 1)) {
			fRec122[l118] = 0.0f;
		}
		for (int l119 = 0; l119 < 3; l119 = faust_wrap_add(l119, 1)) {
			fRec125[l119] = 0.0f;
		}
		for (int l120 = 0; l120 < 2; l120 = faust_wrap_add(l120, 1)) {
			fRec124[l120] = 0.0f;
		}
		for (int l121 = 0; l121 < 3; l121 = faust_wrap_add(l121, 1)) {
			fRec127[l121] = 0.0f;
		}
		for (int l122 = 0; l122 < 2; l122 = faust_wrap_add(l122, 1)) {
			fRec126[l122] = 0.0f;
		}
		for (int l123 = 0; l123 < 3; l123 = faust_wrap_add(l123, 1)) {
			fRec129[l123] = 0.0f;
		}
		for (int l124 = 0; l124 < 2; l124 = faust_wrap_add(l124, 1)) {
			fRec128[l124] = 0.0f;
		}
		for (int l125 = 0; l125 < 3; l125 = faust_wrap_add(l125, 1)) {
			fRec131[l125] = 0.0f;
		}
		for (int l126 = 0; l126 < 2; l126 = faust_wrap_add(l126, 1)) {
			fRec130[l126] = 0.0f;
		}
		for (int l127 = 0; l127 < 3; l127 = faust_wrap_add(l127, 1)) {
			fRec133[l127] = 0.0f;
		}
		for (int l128 = 0; l128 < 2; l128 = faust_wrap_add(l128, 1)) {
			fRec132[l128] = 0.0f;
		}
		for (int l129 = 0; l129 < 3; l129 = faust_wrap_add(l129, 1)) {
			fRec135[l129] = 0.0f;
		}
		for (int l130 = 0; l130 < 2; l130 = faust_wrap_add(l130, 1)) {
			fRec134[l130] = 0.0f;
		}
		for (int l131 = 0; l131 < 3; l131 = faust_wrap_add(l131, 1)) {
			fRec137[l131] = 0.0f;
		}
		for (int l132 = 0; l132 < 2; l132 = faust_wrap_add(l132, 1)) {
			fRec136[l132] = 0.0f;
		}
		for (int l133 = 0; l133 < 3; l133 = faust_wrap_add(l133, 1)) {
			fRec139[l133] = 0.0f;
		}
		for (int l134 = 0; l134 < 2; l134 = faust_wrap_add(l134, 1)) {
			fRec138[l134] = 0.0f;
		}
		for (int l135 = 0; l135 < 2; l135 = faust_wrap_add(l135, 1)) {
			fRec75[l135] = 0.0f;
		}
		for (int l136 = 0; l136 < 2; l136 = faust_wrap_add(l136, 1)) {
			fRec74[l136] = 0.0f;
		}
		for (int l137 = 0; l137 < 2; l137 = faust_wrap_add(l137, 1)) {
			fRec142[l137] = 0.0f;
		}
		for (int l138 = 0; l138 < 2; l138 = faust_wrap_add(l138, 1)) {
			fRec141[l138] = 0.0f;
		}
		for (int l139 = 0; l139 < 3; l139 = faust_wrap_add(l139, 1)) {
			fRec140[l139] = 0.0f;
		}
		for (int l140 = 0; l140 < 3; l140 = faust_wrap_add(l140, 1)) {
			fRec147[l140] = 0.0f;
		}
		for (int l141 = 0; l141 < 2; l141 = faust_wrap_add(l141, 1)) {
			fRec146[l141] = 0.0f;
		}
		for (int l142 = 0; l142 < 3; l142 = faust_wrap_add(l142, 1)) {
			fRec149[l142] = 0.0f;
		}
		for (int l143 = 0; l143 < 2; l143 = faust_wrap_add(l143, 1)) {
			fRec148[l143] = 0.0f;
		}
		for (int l144 = 0; l144 < 3; l144 = faust_wrap_add(l144, 1)) {
			fRec151[l144] = 0.0f;
		}
		for (int l145 = 0; l145 < 2; l145 = faust_wrap_add(l145, 1)) {
			fRec150[l145] = 0.0f;
		}
		for (int l146 = 0; l146 < 3; l146 = faust_wrap_add(l146, 1)) {
			fRec153[l146] = 0.0f;
		}
		for (int l147 = 0; l147 < 2; l147 = faust_wrap_add(l147, 1)) {
			fRec152[l147] = 0.0f;
		}
		for (int l148 = 0; l148 < 3; l148 = faust_wrap_add(l148, 1)) {
			fRec155[l148] = 0.0f;
		}
		for (int l149 = 0; l149 < 2; l149 = faust_wrap_add(l149, 1)) {
			fRec154[l149] = 0.0f;
		}
		for (int l150 = 0; l150 < 3; l150 = faust_wrap_add(l150, 1)) {
			fRec157[l150] = 0.0f;
		}
		for (int l151 = 0; l151 < 2; l151 = faust_wrap_add(l151, 1)) {
			fRec156[l151] = 0.0f;
		}
		for (int l152 = 0; l152 < 3; l152 = faust_wrap_add(l152, 1)) {
			fRec159[l152] = 0.0f;
		}
		for (int l153 = 0; l153 < 2; l153 = faust_wrap_add(l153, 1)) {
			fRec158[l153] = 0.0f;
		}
		for (int l154 = 0; l154 < 3; l154 = faust_wrap_add(l154, 1)) {
			fRec161[l154] = 0.0f;
		}
		for (int l155 = 0; l155 < 2; l155 = faust_wrap_add(l155, 1)) {
			fRec160[l155] = 0.0f;
		}
		for (int l156 = 0; l156 < 3; l156 = faust_wrap_add(l156, 1)) {
			fRec163[l156] = 0.0f;
		}
		for (int l157 = 0; l157 < 2; l157 = faust_wrap_add(l157, 1)) {
			fRec162[l157] = 0.0f;
		}
		for (int l158 = 0; l158 < 3; l158 = faust_wrap_add(l158, 1)) {
			fRec165[l158] = 0.0f;
		}
		for (int l159 = 0; l159 < 2; l159 = faust_wrap_add(l159, 1)) {
			fRec164[l159] = 0.0f;
		}
		for (int l160 = 0; l160 < 3; l160 = faust_wrap_add(l160, 1)) {
			fRec167[l160] = 0.0f;
		}
		for (int l161 = 0; l161 < 2; l161 = faust_wrap_add(l161, 1)) {
			fRec166[l161] = 0.0f;
		}
		for (int l162 = 0; l162 < 3; l162 = faust_wrap_add(l162, 1)) {
			fRec169[l162] = 0.0f;
		}
		for (int l163 = 0; l163 < 2; l163 = faust_wrap_add(l163, 1)) {
			fRec168[l163] = 0.0f;
		}
		for (int l164 = 0; l164 < 3; l164 = faust_wrap_add(l164, 1)) {
			fRec171[l164] = 0.0f;
		}
		for (int l165 = 0; l165 < 2; l165 = faust_wrap_add(l165, 1)) {
			fRec170[l165] = 0.0f;
		}
		for (int l166 = 0; l166 < 3; l166 = faust_wrap_add(l166, 1)) {
			fRec173[l166] = 0.0f;
		}
		for (int l167 = 0; l167 < 2; l167 = faust_wrap_add(l167, 1)) {
			fRec172[l167] = 0.0f;
		}
		for (int l168 = 0; l168 < 3; l168 = faust_wrap_add(l168, 1)) {
			fRec175[l168] = 0.0f;
		}
		for (int l169 = 0; l169 < 2; l169 = faust_wrap_add(l169, 1)) {
			fRec174[l169] = 0.0f;
		}
		for (int l170 = 0; l170 < 3; l170 = faust_wrap_add(l170, 1)) {
			fRec177[l170] = 0.0f;
		}
		for (int l171 = 0; l171 < 2; l171 = faust_wrap_add(l171, 1)) {
			fRec176[l171] = 0.0f;
		}
		for (int l172 = 0; l172 < 3; l172 = faust_wrap_add(l172, 1)) {
			fRec179[l172] = 0.0f;
		}
		for (int l173 = 0; l173 < 2; l173 = faust_wrap_add(l173, 1)) {
			fRec178[l173] = 0.0f;
		}
		for (int l174 = 0; l174 < 3; l174 = faust_wrap_add(l174, 1)) {
			fRec181[l174] = 0.0f;
		}
		for (int l175 = 0; l175 < 2; l175 = faust_wrap_add(l175, 1)) {
			fRec180[l175] = 0.0f;
		}
		for (int l176 = 0; l176 < 3; l176 = faust_wrap_add(l176, 1)) {
			fRec183[l176] = 0.0f;
		}
		for (int l177 = 0; l177 < 2; l177 = faust_wrap_add(l177, 1)) {
			fRec182[l177] = 0.0f;
		}
		for (int l178 = 0; l178 < 3; l178 = faust_wrap_add(l178, 1)) {
			fRec185[l178] = 0.0f;
		}
		for (int l179 = 0; l179 < 2; l179 = faust_wrap_add(l179, 1)) {
			fRec184[l179] = 0.0f;
		}
		for (int l180 = 0; l180 < 3; l180 = faust_wrap_add(l180, 1)) {
			fRec187[l180] = 0.0f;
		}
		for (int l181 = 0; l181 < 2; l181 = faust_wrap_add(l181, 1)) {
			fRec186[l181] = 0.0f;
		}
		for (int l182 = 0; l182 < 3; l182 = faust_wrap_add(l182, 1)) {
			fRec189[l182] = 0.0f;
		}
		for (int l183 = 0; l183 < 2; l183 = faust_wrap_add(l183, 1)) {
			fRec188[l183] = 0.0f;
		}
		for (int l184 = 0; l184 < 3; l184 = faust_wrap_add(l184, 1)) {
			fRec191[l184] = 0.0f;
		}
		for (int l185 = 0; l185 < 2; l185 = faust_wrap_add(l185, 1)) {
			fRec190[l185] = 0.0f;
		}
		for (int l186 = 0; l186 < 3; l186 = faust_wrap_add(l186, 1)) {
			fRec193[l186] = 0.0f;
		}
		for (int l187 = 0; l187 < 2; l187 = faust_wrap_add(l187, 1)) {
			fRec192[l187] = 0.0f;
		}
		for (int l188 = 0; l188 < 3; l188 = faust_wrap_add(l188, 1)) {
			fRec195[l188] = 0.0f;
		}
		for (int l189 = 0; l189 < 2; l189 = faust_wrap_add(l189, 1)) {
			fRec194[l189] = 0.0f;
		}
		for (int l190 = 0; l190 < 3; l190 = faust_wrap_add(l190, 1)) {
			fRec197[l190] = 0.0f;
		}
		for (int l191 = 0; l191 < 2; l191 = faust_wrap_add(l191, 1)) {
			fRec196[l191] = 0.0f;
		}
		for (int l192 = 0; l192 < 3; l192 = faust_wrap_add(l192, 1)) {
			fRec199[l192] = 0.0f;
		}
		for (int l193 = 0; l193 < 2; l193 = faust_wrap_add(l193, 1)) {
			fRec198[l193] = 0.0f;
		}
		for (int l194 = 0; l194 < 3; l194 = faust_wrap_add(l194, 1)) {
			fRec201[l194] = 0.0f;
		}
		for (int l195 = 0; l195 < 2; l195 = faust_wrap_add(l195, 1)) {
			fRec200[l195] = 0.0f;
		}
		for (int l196 = 0; l196 < 3; l196 = faust_wrap_add(l196, 1)) {
			fRec203[l196] = 0.0f;
		}
		for (int l197 = 0; l197 < 2; l197 = faust_wrap_add(l197, 1)) {
			fRec202[l197] = 0.0f;
		}
		for (int l198 = 0; l198 < 3; l198 = faust_wrap_add(l198, 1)) {
			fRec205[l198] = 0.0f;
		}
		for (int l199 = 0; l199 < 2; l199 = faust_wrap_add(l199, 1)) {
			fRec204[l199] = 0.0f;
		}
		for (int l200 = 0; l200 < 3; l200 = faust_wrap_add(l200, 1)) {
			fRec207[l200] = 0.0f;
		}
		for (int l201 = 0; l201 < 2; l201 = faust_wrap_add(l201, 1)) {
			fRec206[l201] = 0.0f;
		}
		for (int l202 = 0; l202 < 3; l202 = faust_wrap_add(l202, 1)) {
			fRec209[l202] = 0.0f;
		}
		for (int l203 = 0; l203 < 2; l203 = faust_wrap_add(l203, 1)) {
			fRec208[l203] = 0.0f;
		}
		for (int l204 = 0; l204 < 2; l204 = faust_wrap_add(l204, 1)) {
			fRec145[l204] = 0.0f;
		}
		for (int l205 = 0; l205 < 2; l205 = faust_wrap_add(l205, 1)) {
			fRec144[l205] = 0.0f;
		}
		for (int l206 = 0; l206 < 2; l206 = faust_wrap_add(l206, 1)) {
			fRec143[l206] = 0.0f;
		}
		for (int l207 = 0; l207 < 3; l207 = faust_wrap_add(l207, 1)) {
			fRec4[l207] = 0.0f;
		}
		for (int l208 = 0; l208 < 2; l208 = faust_wrap_add(l208, 1)) {
			fRec210[l208] = 0.0f;
		}
		for (int l209 = 0; l209 < 3; l209 = faust_wrap_add(l209, 1)) {
			fRec3[l209] = 0.0f;
		}
		for (int l210 = 0; l210 < 3; l210 = faust_wrap_add(l210, 1)) {
			fRec2[l210] = 0.0f;
		}
		for (int l211 = 0; l211 < 3; l211 = faust_wrap_add(l211, 1)) {
			fRec1[l211] = 0.0f;
		}
		for (int l212 = 0; l212 < 3; l212 = faust_wrap_add(l212, 1)) {
			fRec213[l212] = 0.0f;
		}
		for (int l213 = 0; l213 < 3; l213 = faust_wrap_add(l213, 1)) {
			fRec212[l213] = 0.0f;
		}
		for (int l214 = 0; l214 < 3; l214 = faust_wrap_add(l214, 1)) {
			fRec211[l214] = 0.0f;
		}
		for (int l217 = 0; l217 < 2; l217 = faust_wrap_add(l217, 1)) {
			fRec215[l217] = 0.0f;
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
	
	virtual icc_suppressor_f32* clone() {
		return new icc_suppressor_f32(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("icc_suppressor");
		ui_interface->addNumEntry("guard", &fEntry0, FAUSTFLOAT(1e+02f), FAUSTFLOAT(1.0f), FAUSTFLOAT(8e+03f), FAUSTFLOAT(1.0f));
		ui_interface->addNumEntry("hold_time", &fEntry3, FAUSTFLOAT(0.2f), FAUSTFLOAT(0.001f), FAUSTFLOAT(1e+01f), FAUSTFLOAT(0.001f));
		ui_interface->addNumEntry("max_depth", &fEntry4, FAUSTFLOAT(12.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(6e+01f), FAUSTFLOAT(0.1f));
		ui_interface->addNumEntry("notch_bypass", &fEntry1, FAUSTFLOAT(0.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(1.0f));
		ui_interface->addNumEntry("prom_thresh", &fEntry2, FAUSTFLOAT(15.0f), FAUSTFLOAT(0.0f), FAUSTFLOAT(6e+01f), FAUSTFLOAT(0.1f));
		ui_interface->addNumEntry("shift", &fEntry5, FAUSTFLOAT(2.0f), FAUSTFLOAT(-2e+01f), FAUSTFLOAT(2e+01f), FAUSTFLOAT(0.01f));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* output0 = outputs[0];
		float fSlow0 = std::tan(fConst1 * (fConst2 - static_cast<float>(fEntry0)));
		float fSlow1 = 1.0f / fSlow0;
		float fSlow2 = (fSlow1 + 0.5176381f) / fSlow0 + 1.0f;
		float fSlow3 = 2.0f / fSlow2;
		float fSlow4 = 1.0f / ((fSlow1 + 1.4142135f) / fSlow0 + 1.0f);
		float fSlow5 = 1.0f / ((fSlow1 + 1.9318516f) / fSlow0 + 1.0f);
		int iSlow6 = static_cast<int>(static_cast<float>(fEntry1));
		float fSlow7 = static_cast<float>(fEntry2);
		float fSlow8 = static_cast<float>(fEntry3);
		float fSlow9 = 4.0f * fSlow8;
		int iSlow10 = std::fabs(fSlow9) < 1.1920929e-07f;
		float fSlow11 = ((iSlow10) ? 0.0f : std::exp(-(fConst235 / ((iSlow10) ? 1.0f : fSlow9))));
		int iSlow12 = std::fabs(fSlow8) < 1.1920929e-07f;
		float fSlow13 = ((iSlow12) ? 0.0f : std::exp(-(fConst235 / ((iSlow12) ? 1.0f : fSlow8))));
		float fSlow14 = fConst237 * (1.0f - std::pow(1e+01f, -(0.05f * static_cast<float>(fEntry4))));
		float fSlow15 = (fSlow1 + -1.9318516f) / fSlow0 + 1.0f;
		float fSlow16 = 2.0f * (1.0f - 1.0f / icc_suppressor_f32_faustpower2_f(fSlow0));
		float fSlow17 = (fSlow1 + -1.4142135f) / fSlow0 + 1.0f;
		float fSlow18 = 1.0f / fSlow2;
		float fSlow19 = (fSlow1 + -0.5176381f) / fSlow0 + 1.0f;
		float fSlow20 = fConst235 * static_cast<float>(fEntry5);
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			iVec0[0] = 1;
			int iTemp0 = faust_wrap_sub(1, iVec0[1]);
			iRec0[0] = faust_wrap_sub(iTemp0, iRec0[2]);
			float fTemp1 = static_cast<float>(iRec0[0]);
			float fTemp2 = static_cast<float>(input0[i0]);
			float fTemp3 = fTemp2 - fConst13 * (fConst14 * fRec10[2] + fConst15 * fRec10[1]);
			fRec10[0] = ((std::fabs(fTemp3) > 1.1754944e-38f) ? fTemp3 : 0.0f);
			float fTemp4 = std::fabs(fConst12 * (fRec10[0] - fRec10[2]));
			float fTemp5 = ((fTemp4 > fRec9[1]) ? fConst17 : fConst16);
			float fTemp6 = fTemp4 * (1.0f - fTemp5) + fRec9[1] * fTemp5;
			fRec9[0] = ((std::fabs(fTemp6) > 1.1754944e-38f) ? fTemp6 : 0.0f);
			float fTemp7 = fTemp2 - fConst22 * (fConst23 * fRec12[2] + fConst24 * fRec12[1]);
			fRec12[0] = ((std::fabs(fTemp7) > 1.1754944e-38f) ? fTemp7 : 0.0f);
			float fTemp8 = std::fabs(fConst21 * (fRec12[0] - fRec12[2]));
			float fTemp9 = ((fTemp8 > fRec11[1]) ? fConst17 : fConst16);
			float fTemp10 = fTemp8 * (1.0f - fTemp9) + fRec11[1] * fTemp9;
			fRec11[0] = ((std::fabs(fTemp10) > 1.1754944e-38f) ? fTemp10 : 0.0f);
			float fTemp11 = fTemp2 - fConst29 * (fConst30 * fRec14[2] + fConst31 * fRec14[1]);
			fRec14[0] = ((std::fabs(fTemp11) > 1.1754944e-38f) ? fTemp11 : 0.0f);
			float fTemp12 = std::fabs(fConst28 * (fRec14[0] - fRec14[2]));
			float fTemp13 = ((fTemp12 > fRec13[1]) ? fConst17 : fConst16);
			float fTemp14 = fTemp12 * (1.0f - fTemp13) + fRec13[1] * fTemp13;
			fRec13[0] = ((std::fabs(fTemp14) > 1.1754944e-38f) ? fTemp14 : 0.0f);
			float fTemp15 = fTemp2 - fConst36 * (fConst37 * fRec16[2] + fConst38 * fRec16[1]);
			fRec16[0] = ((std::fabs(fTemp15) > 1.1754944e-38f) ? fTemp15 : 0.0f);
			float fTemp16 = std::fabs(fConst35 * (fRec16[0] - fRec16[2]));
			float fTemp17 = ((fTemp16 > fRec15[1]) ? fConst17 : fConst16);
			float fTemp18 = fTemp16 * (1.0f - fTemp17) + fRec15[1] * fTemp17;
			fRec15[0] = ((std::fabs(fTemp18) > 1.1754944e-38f) ? fTemp18 : 0.0f);
			float fTemp19 = fTemp2 - fConst43 * (fConst44 * fRec18[2] + fConst45 * fRec18[1]);
			fRec18[0] = ((std::fabs(fTemp19) > 1.1754944e-38f) ? fTemp19 : 0.0f);
			float fTemp20 = std::fabs(fConst42 * (fRec18[0] - fRec18[2]));
			float fTemp21 = ((fTemp20 > fRec17[1]) ? fConst17 : fConst16);
			float fTemp22 = fTemp20 * (1.0f - fTemp21) + fRec17[1] * fTemp21;
			fRec17[0] = ((std::fabs(fTemp22) > 1.1754944e-38f) ? fTemp22 : 0.0f);
			float fTemp23 = fTemp2 - fConst50 * (fConst51 * fRec20[2] + fConst52 * fRec20[1]);
			fRec20[0] = ((std::fabs(fTemp23) > 1.1754944e-38f) ? fTemp23 : 0.0f);
			float fTemp24 = std::fabs(fConst49 * (fRec20[0] - fRec20[2]));
			float fTemp25 = ((fTemp24 > fRec19[1]) ? fConst17 : fConst16);
			float fTemp26 = fTemp24 * (1.0f - fTemp25) + fRec19[1] * fTemp25;
			fRec19[0] = ((std::fabs(fTemp26) > 1.1754944e-38f) ? fTemp26 : 0.0f);
			float fTemp27 = fTemp2 - fConst57 * (fConst58 * fRec22[2] + fConst59 * fRec22[1]);
			fRec22[0] = ((std::fabs(fTemp27) > 1.1754944e-38f) ? fTemp27 : 0.0f);
			float fTemp28 = std::fabs(fConst56 * (fRec22[0] - fRec22[2]));
			float fTemp29 = ((fTemp28 > fRec21[1]) ? fConst17 : fConst16);
			float fTemp30 = fTemp28 * (1.0f - fTemp29) + fRec21[1] * fTemp29;
			fRec21[0] = ((std::fabs(fTemp30) > 1.1754944e-38f) ? fTemp30 : 0.0f);
			float fTemp31 = fTemp2 - fConst64 * (fConst65 * fRec24[2] + fConst66 * fRec24[1]);
			fRec24[0] = ((std::fabs(fTemp31) > 1.1754944e-38f) ? fTemp31 : 0.0f);
			float fTemp32 = std::fabs(fConst63 * (fRec24[0] - fRec24[2]));
			float fTemp33 = ((fTemp32 > fRec23[1]) ? fConst17 : fConst16);
			float fTemp34 = fTemp32 * (1.0f - fTemp33) + fRec23[1] * fTemp33;
			fRec23[0] = ((std::fabs(fTemp34) > 1.1754944e-38f) ? fTemp34 : 0.0f);
			float fTemp35 = fTemp2 - fConst71 * (fConst72 * fRec26[2] + fConst73 * fRec26[1]);
			fRec26[0] = ((std::fabs(fTemp35) > 1.1754944e-38f) ? fTemp35 : 0.0f);
			float fTemp36 = std::fabs(fConst70 * (fRec26[0] - fRec26[2]));
			float fTemp37 = ((fTemp36 > fRec25[1]) ? fConst17 : fConst16);
			float fTemp38 = fTemp36 * (1.0f - fTemp37) + fRec25[1] * fTemp37;
			fRec25[0] = ((std::fabs(fTemp38) > 1.1754944e-38f) ? fTemp38 : 0.0f);
			float fTemp39 = fTemp2 - fConst78 * (fConst79 * fRec28[2] + fConst80 * fRec28[1]);
			fRec28[0] = ((std::fabs(fTemp39) > 1.1754944e-38f) ? fTemp39 : 0.0f);
			float fTemp40 = std::fabs(fConst77 * (fRec28[0] - fRec28[2]));
			float fTemp41 = ((fTemp40 > fRec27[1]) ? fConst17 : fConst16);
			float fTemp42 = fTemp40 * (1.0f - fTemp41) + fRec27[1] * fTemp41;
			fRec27[0] = ((std::fabs(fTemp42) > 1.1754944e-38f) ? fTemp42 : 0.0f);
			float fTemp43 = fTemp2 - fConst85 * (fConst86 * fRec30[2] + fConst87 * fRec30[1]);
			fRec30[0] = ((std::fabs(fTemp43) > 1.1754944e-38f) ? fTemp43 : 0.0f);
			float fTemp44 = std::fabs(fConst84 * (fRec30[0] - fRec30[2]));
			float fTemp45 = ((fTemp44 > fRec29[1]) ? fConst17 : fConst16);
			float fTemp46 = fTemp44 * (1.0f - fTemp45) + fRec29[1] * fTemp45;
			fRec29[0] = ((std::fabs(fTemp46) > 1.1754944e-38f) ? fTemp46 : 0.0f);
			float fTemp47 = fTemp2 - fConst92 * (fConst93 * fRec32[2] + fConst94 * fRec32[1]);
			fRec32[0] = ((std::fabs(fTemp47) > 1.1754944e-38f) ? fTemp47 : 0.0f);
			float fTemp48 = std::fabs(fConst91 * (fRec32[0] - fRec32[2]));
			float fTemp49 = ((fTemp48 > fRec31[1]) ? fConst17 : fConst16);
			float fTemp50 = fTemp48 * (1.0f - fTemp49) + fRec31[1] * fTemp49;
			fRec31[0] = ((std::fabs(fTemp50) > 1.1754944e-38f) ? fTemp50 : 0.0f);
			float fTemp51 = fTemp2 - fConst99 * (fConst100 * fRec34[2] + fConst101 * fRec34[1]);
			fRec34[0] = ((std::fabs(fTemp51) > 1.1754944e-38f) ? fTemp51 : 0.0f);
			float fTemp52 = std::fabs(fConst98 * (fRec34[0] - fRec34[2]));
			float fTemp53 = ((fTemp52 > fRec33[1]) ? fConst17 : fConst16);
			float fTemp54 = fTemp52 * (1.0f - fTemp53) + fRec33[1] * fTemp53;
			fRec33[0] = ((std::fabs(fTemp54) > 1.1754944e-38f) ? fTemp54 : 0.0f);
			float fTemp55 = fTemp2 - fConst106 * (fConst107 * fRec36[2] + fConst108 * fRec36[1]);
			fRec36[0] = ((std::fabs(fTemp55) > 1.1754944e-38f) ? fTemp55 : 0.0f);
			float fTemp56 = std::fabs(fConst105 * (fRec36[0] - fRec36[2]));
			float fTemp57 = ((fTemp56 > fRec35[1]) ? fConst17 : fConst16);
			float fTemp58 = fTemp56 * (1.0f - fTemp57) + fRec35[1] * fTemp57;
			fRec35[0] = ((std::fabs(fTemp58) > 1.1754944e-38f) ? fTemp58 : 0.0f);
			float fTemp59 = fTemp2 - fConst113 * (fConst114 * fRec38[2] + fConst115 * fRec38[1]);
			fRec38[0] = ((std::fabs(fTemp59) > 1.1754944e-38f) ? fTemp59 : 0.0f);
			float fTemp60 = std::fabs(fConst112 * (fRec38[0] - fRec38[2]));
			float fTemp61 = ((fTemp60 > fRec37[1]) ? fConst17 : fConst16);
			float fTemp62 = fTemp60 * (1.0f - fTemp61) + fRec37[1] * fTemp61;
			fRec37[0] = ((std::fabs(fTemp62) > 1.1754944e-38f) ? fTemp62 : 0.0f);
			float fTemp63 = fTemp2 - fConst120 * (fConst121 * fRec40[2] + fConst122 * fRec40[1]);
			fRec40[0] = ((std::fabs(fTemp63) > 1.1754944e-38f) ? fTemp63 : 0.0f);
			float fTemp64 = std::fabs(fConst119 * (fRec40[0] - fRec40[2]));
			float fTemp65 = ((fTemp64 > fRec39[1]) ? fConst17 : fConst16);
			float fTemp66 = fTemp64 * (1.0f - fTemp65) + fRec39[1] * fTemp65;
			fRec39[0] = ((std::fabs(fTemp66) > 1.1754944e-38f) ? fTemp66 : 0.0f);
			float fTemp67 = fTemp2 - fConst127 * (fConst128 * fRec42[2] + fConst129 * fRec42[1]);
			fRec42[0] = ((std::fabs(fTemp67) > 1.1754944e-38f) ? fTemp67 : 0.0f);
			float fTemp68 = std::fabs(fConst126 * (fRec42[0] - fRec42[2]));
			float fTemp69 = ((fTemp68 > fRec41[1]) ? fConst17 : fConst16);
			float fTemp70 = fTemp68 * (1.0f - fTemp69) + fRec41[1] * fTemp69;
			fRec41[0] = ((std::fabs(fTemp70) > 1.1754944e-38f) ? fTemp70 : 0.0f);
			float fTemp71 = fTemp2 - fConst134 * (fConst135 * fRec44[2] + fConst136 * fRec44[1]);
			fRec44[0] = ((std::fabs(fTemp71) > 1.1754944e-38f) ? fTemp71 : 0.0f);
			float fTemp72 = std::fabs(fConst133 * (fRec44[0] - fRec44[2]));
			float fTemp73 = ((fTemp72 > fRec43[1]) ? fConst17 : fConst16);
			float fTemp74 = fTemp72 * (1.0f - fTemp73) + fRec43[1] * fTemp73;
			fRec43[0] = ((std::fabs(fTemp74) > 1.1754944e-38f) ? fTemp74 : 0.0f);
			float fTemp75 = fTemp2 - fConst141 * (fConst142 * fRec46[2] + fConst143 * fRec46[1]);
			fRec46[0] = ((std::fabs(fTemp75) > 1.1754944e-38f) ? fTemp75 : 0.0f);
			float fTemp76 = std::fabs(fConst140 * (fRec46[0] - fRec46[2]));
			float fTemp77 = ((fTemp76 > fRec45[1]) ? fConst17 : fConst16);
			float fTemp78 = fTemp76 * (1.0f - fTemp77) + fRec45[1] * fTemp77;
			fRec45[0] = ((std::fabs(fTemp78) > 1.1754944e-38f) ? fTemp78 : 0.0f);
			float fTemp79 = fTemp2 - fConst148 * (fConst149 * fRec48[2] + fConst150 * fRec48[1]);
			fRec48[0] = ((std::fabs(fTemp79) > 1.1754944e-38f) ? fTemp79 : 0.0f);
			float fTemp80 = std::fabs(fConst147 * (fRec48[0] - fRec48[2]));
			float fTemp81 = ((fTemp80 > fRec47[1]) ? fConst17 : fConst16);
			float fTemp82 = fTemp80 * (1.0f - fTemp81) + fRec47[1] * fTemp81;
			fRec47[0] = ((std::fabs(fTemp82) > 1.1754944e-38f) ? fTemp82 : 0.0f);
			float fTemp83 = fTemp2 - fConst155 * (fConst156 * fRec50[2] + fConst157 * fRec50[1]);
			fRec50[0] = ((std::fabs(fTemp83) > 1.1754944e-38f) ? fTemp83 : 0.0f);
			float fTemp84 = std::fabs(fConst154 * (fRec50[0] - fRec50[2]));
			float fTemp85 = ((fTemp84 > fRec49[1]) ? fConst17 : fConst16);
			float fTemp86 = fTemp84 * (1.0f - fTemp85) + fRec49[1] * fTemp85;
			fRec49[0] = ((std::fabs(fTemp86) > 1.1754944e-38f) ? fTemp86 : 0.0f);
			float fTemp87 = fTemp2 - fConst162 * (fConst163 * fRec52[2] + fConst164 * fRec52[1]);
			fRec52[0] = ((std::fabs(fTemp87) > 1.1754944e-38f) ? fTemp87 : 0.0f);
			float fTemp88 = std::fabs(fConst161 * (fRec52[0] - fRec52[2]));
			float fTemp89 = ((fTemp88 > fRec51[1]) ? fConst17 : fConst16);
			float fTemp90 = fTemp88 * (1.0f - fTemp89) + fRec51[1] * fTemp89;
			fRec51[0] = ((std::fabs(fTemp90) > 1.1754944e-38f) ? fTemp90 : 0.0f);
			float fTemp91 = fTemp2 - fConst169 * (fConst170 * fRec54[2] + fConst171 * fRec54[1]);
			fRec54[0] = ((std::fabs(fTemp91) > 1.1754944e-38f) ? fTemp91 : 0.0f);
			float fTemp92 = std::fabs(fConst168 * (fRec54[0] - fRec54[2]));
			float fTemp93 = ((fTemp92 > fRec53[1]) ? fConst17 : fConst16);
			float fTemp94 = fTemp92 * (1.0f - fTemp93) + fRec53[1] * fTemp93;
			fRec53[0] = ((std::fabs(fTemp94) > 1.1754944e-38f) ? fTemp94 : 0.0f);
			float fTemp95 = fTemp2 - fConst176 * (fConst177 * fRec56[2] + fConst178 * fRec56[1]);
			fRec56[0] = ((std::fabs(fTemp95) > 1.1754944e-38f) ? fTemp95 : 0.0f);
			float fTemp96 = std::fabs(fConst175 * (fRec56[0] - fRec56[2]));
			float fTemp97 = ((fTemp96 > fRec55[1]) ? fConst17 : fConst16);
			float fTemp98 = fTemp96 * (1.0f - fTemp97) + fRec55[1] * fTemp97;
			fRec55[0] = ((std::fabs(fTemp98) > 1.1754944e-38f) ? fTemp98 : 0.0f);
			float fTemp99 = fTemp2 - fConst183 * (fConst184 * fRec58[2] + fConst185 * fRec58[1]);
			fRec58[0] = ((std::fabs(fTemp99) > 1.1754944e-38f) ? fTemp99 : 0.0f);
			float fTemp100 = std::fabs(fConst182 * (fRec58[0] - fRec58[2]));
			float fTemp101 = ((fTemp100 > fRec57[1]) ? fConst17 : fConst16);
			float fTemp102 = fTemp100 * (1.0f - fTemp101) + fRec57[1] * fTemp101;
			fRec57[0] = ((std::fabs(fTemp102) > 1.1754944e-38f) ? fTemp102 : 0.0f);
			float fTemp103 = fTemp2 - fConst190 * (fConst191 * fRec60[2] + fConst192 * fRec60[1]);
			fRec60[0] = ((std::fabs(fTemp103) > 1.1754944e-38f) ? fTemp103 : 0.0f);
			float fTemp104 = std::fabs(fConst189 * (fRec60[0] - fRec60[2]));
			float fTemp105 = ((fTemp104 > fRec59[1]) ? fConst17 : fConst16);
			float fTemp106 = fTemp104 * (1.0f - fTemp105) + fRec59[1] * fTemp105;
			fRec59[0] = ((std::fabs(fTemp106) > 1.1754944e-38f) ? fTemp106 : 0.0f);
			float fTemp107 = fTemp2 - fConst197 * (fConst198 * fRec62[2] + fConst199 * fRec62[1]);
			fRec62[0] = ((std::fabs(fTemp107) > 1.1754944e-38f) ? fTemp107 : 0.0f);
			float fTemp108 = std::fabs(fConst196 * (fRec62[0] - fRec62[2]));
			float fTemp109 = ((fTemp108 > fRec61[1]) ? fConst17 : fConst16);
			float fTemp110 = fTemp108 * (1.0f - fTemp109) + fRec61[1] * fTemp109;
			fRec61[0] = ((std::fabs(fTemp110) > 1.1754944e-38f) ? fTemp110 : 0.0f);
			float fTemp111 = fTemp2 - fConst204 * (fConst205 * fRec64[2] + fConst206 * fRec64[1]);
			fRec64[0] = ((std::fabs(fTemp111) > 1.1754944e-38f) ? fTemp111 : 0.0f);
			float fTemp112 = std::fabs(fConst203 * (fRec64[0] - fRec64[2]));
			float fTemp113 = ((fTemp112 > fRec63[1]) ? fConst17 : fConst16);
			float fTemp114 = fTemp112 * (1.0f - fTemp113) + fRec63[1] * fTemp113;
			fRec63[0] = ((std::fabs(fTemp114) > 1.1754944e-38f) ? fTemp114 : 0.0f);
			float fTemp115 = fTemp2 - fConst211 * (fConst212 * fRec66[2] + fConst213 * fRec66[1]);
			fRec66[0] = ((std::fabs(fTemp115) > 1.1754944e-38f) ? fTemp115 : 0.0f);
			float fTemp116 = std::fabs(fConst210 * (fRec66[0] - fRec66[2]));
			float fTemp117 = ((fTemp116 > fRec65[1]) ? fConst17 : fConst16);
			float fTemp118 = fTemp116 * (1.0f - fTemp117) + fRec65[1] * fTemp117;
			fRec65[0] = ((std::fabs(fTemp118) > 1.1754944e-38f) ? fTemp118 : 0.0f);
			float fTemp119 = fTemp2 - fConst218 * (fConst219 * fRec68[2] + fConst220 * fRec68[1]);
			fRec68[0] = ((std::fabs(fTemp119) > 1.1754944e-38f) ? fTemp119 : 0.0f);
			float fTemp120 = std::fabs(fConst217 * (fRec68[0] - fRec68[2]));
			float fTemp121 = ((fTemp120 > fRec67[1]) ? fConst17 : fConst16);
			float fTemp122 = fTemp120 * (1.0f - fTemp121) + fRec67[1] * fTemp121;
			fRec67[0] = ((std::fabs(fTemp122) > 1.1754944e-38f) ? fTemp122 : 0.0f);
			float fTemp123 = fTemp2 - fConst225 * (fConst226 * fRec70[2] + fConst227 * fRec70[1]);
			fRec70[0] = ((std::fabs(fTemp123) > 1.1754944e-38f) ? fTemp123 : 0.0f);
			float fTemp124 = std::fabs(fConst224 * (fRec70[0] - fRec70[2]));
			float fTemp125 = ((fTemp124 > fRec69[1]) ? fConst17 : fConst16);
			float fTemp126 = fTemp124 * (1.0f - fTemp125) + fRec69[1] * fTemp125;
			fRec69[0] = ((std::fabs(fTemp126) > 1.1754944e-38f) ? fTemp126 : 0.0f);
			float fTemp127 = fTemp2 - fConst232 * (fConst233 * fRec72[2] + fConst234 * fRec72[1]);
			fRec72[0] = ((std::fabs(fTemp127) > 1.1754944e-38f) ? fTemp127 : 0.0f);
			float fTemp128 = std::fabs(fConst231 * (fRec72[0] - fRec72[2]));
			float fTemp129 = ((fTemp128 > fRec71[1]) ? fConst17 : fConst16);
			float fTemp130 = fTemp128 * (1.0f - fTemp129) + fRec71[1] * fTemp129;
			fRec71[0] = ((std::fabs(fTemp130) > 1.1754944e-38f) ? fTemp130 : 0.0f);
			int iTemp131 = fRec69[0] > fRec71[0];
			float fTemp132 = ((iTemp131) ? fRec69[0] : fRec71[0]);
			int iTemp133 = fRec67[0] > fTemp132;
			float fTemp134 = ((iTemp133) ? fRec67[0] : fTemp132);
			int iTemp135 = fRec65[0] > fTemp134;
			float fTemp136 = ((iTemp135) ? fRec65[0] : fTemp134);
			int iTemp137 = fRec63[0] > fTemp136;
			float fTemp138 = ((iTemp137) ? fRec63[0] : fTemp136);
			int iTemp139 = fRec61[0] > fTemp138;
			float fTemp140 = ((iTemp139) ? fRec61[0] : fTemp138);
			int iTemp141 = fRec59[0] > fTemp140;
			float fTemp142 = ((iTemp141) ? fRec59[0] : fTemp140);
			int iTemp143 = fRec57[0] > fTemp142;
			float fTemp144 = ((iTemp143) ? fRec57[0] : fTemp142);
			int iTemp145 = fRec55[0] > fTemp144;
			float fTemp146 = ((iTemp145) ? fRec55[0] : fTemp144);
			int iTemp147 = fRec53[0] > fTemp146;
			float fTemp148 = ((iTemp147) ? fRec53[0] : fTemp146);
			int iTemp149 = fRec51[0] > fTemp148;
			float fTemp150 = ((iTemp149) ? fRec51[0] : fTemp148);
			int iTemp151 = fRec49[0] > fTemp150;
			float fTemp152 = ((iTemp151) ? fRec49[0] : fTemp150);
			int iTemp153 = fRec47[0] > fTemp152;
			float fTemp154 = ((iTemp153) ? fRec47[0] : fTemp152);
			int iTemp155 = fRec45[0] > fTemp154;
			float fTemp156 = ((iTemp155) ? fRec45[0] : fTemp154);
			int iTemp157 = fRec43[0] > fTemp156;
			float fTemp158 = ((iTemp157) ? fRec43[0] : fTemp156);
			int iTemp159 = fRec41[0] > fTemp158;
			float fTemp160 = ((iTemp159) ? fRec41[0] : fTemp158);
			int iTemp161 = fRec39[0] > fTemp160;
			float fTemp162 = ((iTemp161) ? fRec39[0] : fTemp160);
			int iTemp163 = fRec37[0] > fTemp162;
			float fTemp164 = ((iTemp163) ? fRec37[0] : fTemp162);
			int iTemp165 = fRec35[0] > fTemp164;
			float fTemp166 = ((iTemp165) ? fRec35[0] : fTemp164);
			int iTemp167 = fRec33[0] > fTemp166;
			float fTemp168 = ((iTemp167) ? fRec33[0] : fTemp166);
			int iTemp169 = fRec31[0] > fTemp168;
			float fTemp170 = ((iTemp169) ? fRec31[0] : fTemp168);
			int iTemp171 = fRec29[0] > fTemp170;
			float fTemp172 = ((iTemp171) ? fRec29[0] : fTemp170);
			int iTemp173 = fRec27[0] > fTemp172;
			float fTemp174 = ((iTemp173) ? fRec27[0] : fTemp172);
			int iTemp175 = fRec25[0] > fTemp174;
			float fTemp176 = ((iTemp175) ? fRec25[0] : fTemp174);
			int iTemp177 = fRec23[0] > fTemp176;
			float fTemp178 = ((iTemp177) ? fRec23[0] : fTemp176);
			int iTemp179 = fRec21[0] > fTemp178;
			float fTemp180 = ((iTemp179) ? fRec21[0] : fTemp178);
			int iTemp181 = fRec19[0] > fTemp180;
			float fTemp182 = ((iTemp181) ? fRec19[0] : fTemp180);
			int iTemp183 = fRec17[0] > fTemp182;
			float fTemp184 = ((iTemp183) ? fRec17[0] : fTemp182);
			int iTemp185 = fRec15[0] > fTemp184;
			float fTemp186 = ((iTemp185) ? fRec15[0] : fTemp184);
			int iTemp187 = fRec13[0] > fTemp186;
			float fTemp188 = ((iTemp187) ? fRec13[0] : fTemp186);
			int iTemp189 = fRec11[0] > fTemp188;
			float fTemp190 = ((iTemp189) ? fRec11[0] : fTemp188);
			int iTemp191 = fRec9[0] > fTemp190;
			float fTemp192 = static_cast<float>(std::abs((2e+01f * std::log10((1e-09f + ((iTemp191) ? fRec9[0] : fTemp190)) / (1e-09f + 0.03125f * (fRec71[0] + fRec69[0] + fRec67[0] + fRec65[0] + fRec63[0] + fRec61[0] + fRec59[0] + fRec57[0] + fRec55[0] + fRec53[0] + fRec51[0] + fRec49[0] + fRec47[0] + fRec45[0] + fRec43[0] + fRec41[0] + fRec39[0] + fRec37[0] + fRec35[0] + fRec33[0] + fRec31[0] + fRec29[0] + fRec27[0] + fRec25[0] + fRec23[0] + fRec21[0] + fRec19[0] + fRec17[0] + fRec15[0] + fRec13[0] + fRec9[0] + fRec11[0])))) > fSlow7));
			float fTemp193 = ((fTemp192 > fRec8[1]) ? fSlow13 : fSlow11);
			float fTemp194 = fTemp192 * (1.0f - fTemp193) + fRec8[1] * fTemp193;
			fRec8[0] = ((std::fabs(fTemp194) > 1.1754944e-38f) ? fTemp194 : 0.0f);
			float fTemp195 = ((fRec8[0] > 0.5f) ? ((iTemp191) ? 6e+03f : ((iTemp189) ? 5326.8677f : ((iTemp187) ? 4729.253f : ((iTemp185) ? 4198.684f : ((iTemp183) ? 3727.639f : ((iTemp181) ? 3309.4397f : ((iTemp179) ? 2938.158f : ((iTemp177) ? 2608.5295f : ((iTemp175) ? 2315.8818f : ((iTemp173) ? 2056.066f : ((iTemp171) ? 1825.3986f : ((iTemp169) ? 1620.6094f : ((iTemp167) ? 1438.7953f : ((iTemp165) ? 1277.3787f : ((iTemp163) ? 1134.0712f : ((iTemp161) ? 1006.84106f : ((iTemp159) ? 893.8848f : ((iTemp157) ? 793.601f : ((iTemp155) ? 704.56793f : ((iTemp153) ? 625.5233f : ((iTemp151) ? 555.3467f : ((iTemp149) ? 493.043f : ((iTemp147) ? 437.72913f : ((iTemp145) ? 388.62085f : ((iTemp143) ? 345.02197f : ((iTemp141) ? 306.3144f : ((iTemp139) ? 271.94934f : ((iTemp137) ? 241.4397f : ((iTemp135) ? 214.35287f : ((iTemp133) ? 190.3049f : ((iTemp131) ? 168.95483f : 1.5e+02f))))))))))))))))))))))))))))))) : fRec7[1]);
			fRec7[0] = ((std::fabs(fTemp195) > 1.1754944e-38f) ? fTemp195 : 0.0f);
			float fTemp196 = fConst8 * std::min<float>(6e+03f, std::max<float>(1.5e+02f, fRec7[0])) + fConst7 * fRec6[1];
			fRec6[0] = ((std::fabs(fTemp196) > 1.1754944e-38f) ? fTemp196 : 0.0f);
			float fTemp197 = fRec5[1] * std::cos(fConst6 * fRec6[0]);
			float fTemp198 = fTemp2 - fConst4 * fRec5[2] + fConst5 * fTemp197;
			fRec5[0] = ((std::fabs(fTemp198) > 1.1754944e-38f) ? fTemp198 : 0.0f);
			float fTemp199 = fSlow14 * std::max<float>(0.0f, std::min<float>(1.0f, fRec8[0])) + fConst236 * fRec73[1];
			fRec73[0] = ((std::fabs(fTemp199) > 1.1754944e-38f) ? fTemp199 : 0.0f);
			float fTemp200 = (0.5f * fRec5[0] - fTemp197 + 0.5f * fRec5[2]) * fRec73[0];
			float fTemp201 = fTemp2 * (1.0f - fRec73[0]);
			float fTemp202 = fConst5 * fTemp200 + fTemp201;
			float fTemp203 = fTemp202 - fConst13 * (fConst14 * fRec77[2] + fConst15 * fRec77[1]);
			fRec77[0] = ((std::fabs(fTemp203) > 1.1754944e-38f) ? fTemp203 : 0.0f);
			float fTemp204 = std::fabs(fConst12 * (fRec77[0] - fRec77[2]));
			float fTemp205 = ((fTemp204 > fRec76[1]) ? fConst17 : fConst16);
			float fTemp206 = fTemp204 * (1.0f - fTemp205) + fRec76[1] * fTemp205;
			fRec76[0] = ((std::fabs(fTemp206) > 1.1754944e-38f) ? fTemp206 : 0.0f);
			float fTemp207 = fTemp202 - fConst22 * (fConst23 * fRec79[2] + fConst24 * fRec79[1]);
			fRec79[0] = ((std::fabs(fTemp207) > 1.1754944e-38f) ? fTemp207 : 0.0f);
			float fTemp208 = std::fabs(fConst21 * (fRec79[0] - fRec79[2]));
			float fTemp209 = ((fTemp208 > fRec78[1]) ? fConst17 : fConst16);
			float fTemp210 = fTemp208 * (1.0f - fTemp209) + fRec78[1] * fTemp209;
			fRec78[0] = ((std::fabs(fTemp210) > 1.1754944e-38f) ? fTemp210 : 0.0f);
			float fTemp211 = fTemp202 - fConst29 * (fConst30 * fRec81[2] + fConst31 * fRec81[1]);
			fRec81[0] = ((std::fabs(fTemp211) > 1.1754944e-38f) ? fTemp211 : 0.0f);
			float fTemp212 = std::fabs(fConst28 * (fRec81[0] - fRec81[2]));
			float fTemp213 = ((fTemp212 > fRec80[1]) ? fConst17 : fConst16);
			float fTemp214 = fTemp212 * (1.0f - fTemp213) + fRec80[1] * fTemp213;
			fRec80[0] = ((std::fabs(fTemp214) > 1.1754944e-38f) ? fTemp214 : 0.0f);
			float fTemp215 = fTemp202 - fConst36 * (fConst37 * fRec83[2] + fConst38 * fRec83[1]);
			fRec83[0] = ((std::fabs(fTemp215) > 1.1754944e-38f) ? fTemp215 : 0.0f);
			float fTemp216 = std::fabs(fConst35 * (fRec83[0] - fRec83[2]));
			float fTemp217 = ((fTemp216 > fRec82[1]) ? fConst17 : fConst16);
			float fTemp218 = fTemp216 * (1.0f - fTemp217) + fRec82[1] * fTemp217;
			fRec82[0] = ((std::fabs(fTemp218) > 1.1754944e-38f) ? fTemp218 : 0.0f);
			float fTemp219 = fTemp202 - fConst43 * (fConst44 * fRec85[2] + fConst45 * fRec85[1]);
			fRec85[0] = ((std::fabs(fTemp219) > 1.1754944e-38f) ? fTemp219 : 0.0f);
			float fTemp220 = std::fabs(fConst42 * (fRec85[0] - fRec85[2]));
			float fTemp221 = ((fTemp220 > fRec84[1]) ? fConst17 : fConst16);
			float fTemp222 = fTemp220 * (1.0f - fTemp221) + fRec84[1] * fTemp221;
			fRec84[0] = ((std::fabs(fTemp222) > 1.1754944e-38f) ? fTemp222 : 0.0f);
			float fTemp223 = fTemp202 - fConst50 * (fConst51 * fRec87[2] + fConst52 * fRec87[1]);
			fRec87[0] = ((std::fabs(fTemp223) > 1.1754944e-38f) ? fTemp223 : 0.0f);
			float fTemp224 = std::fabs(fConst49 * (fRec87[0] - fRec87[2]));
			float fTemp225 = ((fTemp224 > fRec86[1]) ? fConst17 : fConst16);
			float fTemp226 = fTemp224 * (1.0f - fTemp225) + fRec86[1] * fTemp225;
			fRec86[0] = ((std::fabs(fTemp226) > 1.1754944e-38f) ? fTemp226 : 0.0f);
			float fTemp227 = fTemp202 - fConst57 * (fConst58 * fRec89[2] + fConst59 * fRec89[1]);
			fRec89[0] = ((std::fabs(fTemp227) > 1.1754944e-38f) ? fTemp227 : 0.0f);
			float fTemp228 = std::fabs(fConst56 * (fRec89[0] - fRec89[2]));
			float fTemp229 = ((fTemp228 > fRec88[1]) ? fConst17 : fConst16);
			float fTemp230 = fTemp228 * (1.0f - fTemp229) + fRec88[1] * fTemp229;
			fRec88[0] = ((std::fabs(fTemp230) > 1.1754944e-38f) ? fTemp230 : 0.0f);
			float fTemp231 = fTemp202 - fConst64 * (fConst65 * fRec91[2] + fConst66 * fRec91[1]);
			fRec91[0] = ((std::fabs(fTemp231) > 1.1754944e-38f) ? fTemp231 : 0.0f);
			float fTemp232 = std::fabs(fConst63 * (fRec91[0] - fRec91[2]));
			float fTemp233 = ((fTemp232 > fRec90[1]) ? fConst17 : fConst16);
			float fTemp234 = fTemp232 * (1.0f - fTemp233) + fRec90[1] * fTemp233;
			fRec90[0] = ((std::fabs(fTemp234) > 1.1754944e-38f) ? fTemp234 : 0.0f);
			float fTemp235 = fTemp202 - fConst71 * (fConst72 * fRec93[2] + fConst73 * fRec93[1]);
			fRec93[0] = ((std::fabs(fTemp235) > 1.1754944e-38f) ? fTemp235 : 0.0f);
			float fTemp236 = std::fabs(fConst70 * (fRec93[0] - fRec93[2]));
			float fTemp237 = ((fTemp236 > fRec92[1]) ? fConst17 : fConst16);
			float fTemp238 = fTemp236 * (1.0f - fTemp237) + fRec92[1] * fTemp237;
			fRec92[0] = ((std::fabs(fTemp238) > 1.1754944e-38f) ? fTemp238 : 0.0f);
			float fTemp239 = fTemp202 - fConst78 * (fConst79 * fRec95[2] + fConst80 * fRec95[1]);
			fRec95[0] = ((std::fabs(fTemp239) > 1.1754944e-38f) ? fTemp239 : 0.0f);
			float fTemp240 = std::fabs(fConst77 * (fRec95[0] - fRec95[2]));
			float fTemp241 = ((fTemp240 > fRec94[1]) ? fConst17 : fConst16);
			float fTemp242 = fTemp240 * (1.0f - fTemp241) + fRec94[1] * fTemp241;
			fRec94[0] = ((std::fabs(fTemp242) > 1.1754944e-38f) ? fTemp242 : 0.0f);
			float fTemp243 = fTemp202 - fConst85 * (fConst86 * fRec97[2] + fConst87 * fRec97[1]);
			fRec97[0] = ((std::fabs(fTemp243) > 1.1754944e-38f) ? fTemp243 : 0.0f);
			float fTemp244 = std::fabs(fConst84 * (fRec97[0] - fRec97[2]));
			float fTemp245 = ((fTemp244 > fRec96[1]) ? fConst17 : fConst16);
			float fTemp246 = fTemp244 * (1.0f - fTemp245) + fRec96[1] * fTemp245;
			fRec96[0] = ((std::fabs(fTemp246) > 1.1754944e-38f) ? fTemp246 : 0.0f);
			float fTemp247 = fTemp202 - fConst92 * (fConst93 * fRec99[2] + fConst94 * fRec99[1]);
			fRec99[0] = ((std::fabs(fTemp247) > 1.1754944e-38f) ? fTemp247 : 0.0f);
			float fTemp248 = std::fabs(fConst91 * (fRec99[0] - fRec99[2]));
			float fTemp249 = ((fTemp248 > fRec98[1]) ? fConst17 : fConst16);
			float fTemp250 = fTemp248 * (1.0f - fTemp249) + fRec98[1] * fTemp249;
			fRec98[0] = ((std::fabs(fTemp250) > 1.1754944e-38f) ? fTemp250 : 0.0f);
			float fTemp251 = fTemp202 - fConst99 * (fConst100 * fRec101[2] + fConst101 * fRec101[1]);
			fRec101[0] = ((std::fabs(fTemp251) > 1.1754944e-38f) ? fTemp251 : 0.0f);
			float fTemp252 = std::fabs(fConst98 * (fRec101[0] - fRec101[2]));
			float fTemp253 = ((fTemp252 > fRec100[1]) ? fConst17 : fConst16);
			float fTemp254 = fTemp252 * (1.0f - fTemp253) + fRec100[1] * fTemp253;
			fRec100[0] = ((std::fabs(fTemp254) > 1.1754944e-38f) ? fTemp254 : 0.0f);
			float fTemp255 = fTemp202 - fConst106 * (fConst107 * fRec103[2] + fConst108 * fRec103[1]);
			fRec103[0] = ((std::fabs(fTemp255) > 1.1754944e-38f) ? fTemp255 : 0.0f);
			float fTemp256 = std::fabs(fConst105 * (fRec103[0] - fRec103[2]));
			float fTemp257 = ((fTemp256 > fRec102[1]) ? fConst17 : fConst16);
			float fTemp258 = fTemp256 * (1.0f - fTemp257) + fRec102[1] * fTemp257;
			fRec102[0] = ((std::fabs(fTemp258) > 1.1754944e-38f) ? fTemp258 : 0.0f);
			float fTemp259 = fTemp202 - fConst113 * (fConst114 * fRec105[2] + fConst115 * fRec105[1]);
			fRec105[0] = ((std::fabs(fTemp259) > 1.1754944e-38f) ? fTemp259 : 0.0f);
			float fTemp260 = std::fabs(fConst112 * (fRec105[0] - fRec105[2]));
			float fTemp261 = ((fTemp260 > fRec104[1]) ? fConst17 : fConst16);
			float fTemp262 = fTemp260 * (1.0f - fTemp261) + fRec104[1] * fTemp261;
			fRec104[0] = ((std::fabs(fTemp262) > 1.1754944e-38f) ? fTemp262 : 0.0f);
			float fTemp263 = fTemp202 - fConst120 * (fConst121 * fRec107[2] + fConst122 * fRec107[1]);
			fRec107[0] = ((std::fabs(fTemp263) > 1.1754944e-38f) ? fTemp263 : 0.0f);
			float fTemp264 = std::fabs(fConst119 * (fRec107[0] - fRec107[2]));
			float fTemp265 = ((fTemp264 > fRec106[1]) ? fConst17 : fConst16);
			float fTemp266 = fTemp264 * (1.0f - fTemp265) + fRec106[1] * fTemp265;
			fRec106[0] = ((std::fabs(fTemp266) > 1.1754944e-38f) ? fTemp266 : 0.0f);
			float fTemp267 = fTemp202 - fConst127 * (fConst128 * fRec109[2] + fConst129 * fRec109[1]);
			fRec109[0] = ((std::fabs(fTemp267) > 1.1754944e-38f) ? fTemp267 : 0.0f);
			float fTemp268 = std::fabs(fConst126 * (fRec109[0] - fRec109[2]));
			float fTemp269 = ((fTemp268 > fRec108[1]) ? fConst17 : fConst16);
			float fTemp270 = fTemp268 * (1.0f - fTemp269) + fRec108[1] * fTemp269;
			fRec108[0] = ((std::fabs(fTemp270) > 1.1754944e-38f) ? fTemp270 : 0.0f);
			float fTemp271 = fTemp202 - fConst134 * (fConst135 * fRec111[2] + fConst136 * fRec111[1]);
			fRec111[0] = ((std::fabs(fTemp271) > 1.1754944e-38f) ? fTemp271 : 0.0f);
			float fTemp272 = std::fabs(fConst133 * (fRec111[0] - fRec111[2]));
			float fTemp273 = ((fTemp272 > fRec110[1]) ? fConst17 : fConst16);
			float fTemp274 = fTemp272 * (1.0f - fTemp273) + fRec110[1] * fTemp273;
			fRec110[0] = ((std::fabs(fTemp274) > 1.1754944e-38f) ? fTemp274 : 0.0f);
			float fTemp275 = fTemp202 - fConst141 * (fConst142 * fRec113[2] + fConst143 * fRec113[1]);
			fRec113[0] = ((std::fabs(fTemp275) > 1.1754944e-38f) ? fTemp275 : 0.0f);
			float fTemp276 = std::fabs(fConst140 * (fRec113[0] - fRec113[2]));
			float fTemp277 = ((fTemp276 > fRec112[1]) ? fConst17 : fConst16);
			float fTemp278 = fTemp276 * (1.0f - fTemp277) + fRec112[1] * fTemp277;
			fRec112[0] = ((std::fabs(fTemp278) > 1.1754944e-38f) ? fTemp278 : 0.0f);
			float fTemp279 = fTemp202 - fConst148 * (fConst149 * fRec115[2] + fConst150 * fRec115[1]);
			fRec115[0] = ((std::fabs(fTemp279) > 1.1754944e-38f) ? fTemp279 : 0.0f);
			float fTemp280 = std::fabs(fConst147 * (fRec115[0] - fRec115[2]));
			float fTemp281 = ((fTemp280 > fRec114[1]) ? fConst17 : fConst16);
			float fTemp282 = fTemp280 * (1.0f - fTemp281) + fRec114[1] * fTemp281;
			fRec114[0] = ((std::fabs(fTemp282) > 1.1754944e-38f) ? fTemp282 : 0.0f);
			float fTemp283 = fTemp202 - fConst155 * (fConst156 * fRec117[2] + fConst157 * fRec117[1]);
			fRec117[0] = ((std::fabs(fTemp283) > 1.1754944e-38f) ? fTemp283 : 0.0f);
			float fTemp284 = std::fabs(fConst154 * (fRec117[0] - fRec117[2]));
			float fTemp285 = ((fTemp284 > fRec116[1]) ? fConst17 : fConst16);
			float fTemp286 = fTemp284 * (1.0f - fTemp285) + fRec116[1] * fTemp285;
			fRec116[0] = ((std::fabs(fTemp286) > 1.1754944e-38f) ? fTemp286 : 0.0f);
			float fTemp287 = fTemp202 - fConst162 * (fConst163 * fRec119[2] + fConst164 * fRec119[1]);
			fRec119[0] = ((std::fabs(fTemp287) > 1.1754944e-38f) ? fTemp287 : 0.0f);
			float fTemp288 = std::fabs(fConst161 * (fRec119[0] - fRec119[2]));
			float fTemp289 = ((fTemp288 > fRec118[1]) ? fConst17 : fConst16);
			float fTemp290 = fTemp288 * (1.0f - fTemp289) + fRec118[1] * fTemp289;
			fRec118[0] = ((std::fabs(fTemp290) > 1.1754944e-38f) ? fTemp290 : 0.0f);
			float fTemp291 = fTemp202 - fConst169 * (fConst170 * fRec121[2] + fConst171 * fRec121[1]);
			fRec121[0] = ((std::fabs(fTemp291) > 1.1754944e-38f) ? fTemp291 : 0.0f);
			float fTemp292 = std::fabs(fConst168 * (fRec121[0] - fRec121[2]));
			float fTemp293 = ((fTemp292 > fRec120[1]) ? fConst17 : fConst16);
			float fTemp294 = fTemp292 * (1.0f - fTemp293) + fRec120[1] * fTemp293;
			fRec120[0] = ((std::fabs(fTemp294) > 1.1754944e-38f) ? fTemp294 : 0.0f);
			float fTemp295 = fTemp202 - fConst176 * (fConst177 * fRec123[2] + fConst178 * fRec123[1]);
			fRec123[0] = ((std::fabs(fTemp295) > 1.1754944e-38f) ? fTemp295 : 0.0f);
			float fTemp296 = std::fabs(fConst175 * (fRec123[0] - fRec123[2]));
			float fTemp297 = ((fTemp296 > fRec122[1]) ? fConst17 : fConst16);
			float fTemp298 = fTemp296 * (1.0f - fTemp297) + fRec122[1] * fTemp297;
			fRec122[0] = ((std::fabs(fTemp298) > 1.1754944e-38f) ? fTemp298 : 0.0f);
			float fTemp299 = fTemp202 - fConst183 * (fConst184 * fRec125[2] + fConst185 * fRec125[1]);
			fRec125[0] = ((std::fabs(fTemp299) > 1.1754944e-38f) ? fTemp299 : 0.0f);
			float fTemp300 = std::fabs(fConst182 * (fRec125[0] - fRec125[2]));
			float fTemp301 = ((fTemp300 > fRec124[1]) ? fConst17 : fConst16);
			float fTemp302 = fTemp300 * (1.0f - fTemp301) + fRec124[1] * fTemp301;
			fRec124[0] = ((std::fabs(fTemp302) > 1.1754944e-38f) ? fTemp302 : 0.0f);
			float fTemp303 = fTemp202 - fConst190 * (fConst191 * fRec127[2] + fConst192 * fRec127[1]);
			fRec127[0] = ((std::fabs(fTemp303) > 1.1754944e-38f) ? fTemp303 : 0.0f);
			float fTemp304 = std::fabs(fConst189 * (fRec127[0] - fRec127[2]));
			float fTemp305 = ((fTemp304 > fRec126[1]) ? fConst17 : fConst16);
			float fTemp306 = fTemp304 * (1.0f - fTemp305) + fRec126[1] * fTemp305;
			fRec126[0] = ((std::fabs(fTemp306) > 1.1754944e-38f) ? fTemp306 : 0.0f);
			float fTemp307 = fTemp202 - fConst197 * (fConst198 * fRec129[2] + fConst199 * fRec129[1]);
			fRec129[0] = ((std::fabs(fTemp307) > 1.1754944e-38f) ? fTemp307 : 0.0f);
			float fTemp308 = std::fabs(fConst196 * (fRec129[0] - fRec129[2]));
			float fTemp309 = ((fTemp308 > fRec128[1]) ? fConst17 : fConst16);
			float fTemp310 = fTemp308 * (1.0f - fTemp309) + fRec128[1] * fTemp309;
			fRec128[0] = ((std::fabs(fTemp310) > 1.1754944e-38f) ? fTemp310 : 0.0f);
			float fTemp311 = fTemp202 - fConst204 * (fConst205 * fRec131[2] + fConst206 * fRec131[1]);
			fRec131[0] = ((std::fabs(fTemp311) > 1.1754944e-38f) ? fTemp311 : 0.0f);
			float fTemp312 = std::fabs(fConst203 * (fRec131[0] - fRec131[2]));
			float fTemp313 = ((fTemp312 > fRec130[1]) ? fConst17 : fConst16);
			float fTemp314 = fTemp312 * (1.0f - fTemp313) + fRec130[1] * fTemp313;
			fRec130[0] = ((std::fabs(fTemp314) > 1.1754944e-38f) ? fTemp314 : 0.0f);
			float fTemp315 = fTemp202 - fConst211 * (fConst212 * fRec133[2] + fConst213 * fRec133[1]);
			fRec133[0] = ((std::fabs(fTemp315) > 1.1754944e-38f) ? fTemp315 : 0.0f);
			float fTemp316 = std::fabs(fConst210 * (fRec133[0] - fRec133[2]));
			float fTemp317 = ((fTemp316 > fRec132[1]) ? fConst17 : fConst16);
			float fTemp318 = fTemp316 * (1.0f - fTemp317) + fRec132[1] * fTemp317;
			fRec132[0] = ((std::fabs(fTemp318) > 1.1754944e-38f) ? fTemp318 : 0.0f);
			float fTemp319 = fTemp202 - fConst218 * (fConst219 * fRec135[2] + fConst220 * fRec135[1]);
			fRec135[0] = ((std::fabs(fTemp319) > 1.1754944e-38f) ? fTemp319 : 0.0f);
			float fTemp320 = std::fabs(fConst217 * (fRec135[0] - fRec135[2]));
			float fTemp321 = ((fTemp320 > fRec134[1]) ? fConst17 : fConst16);
			float fTemp322 = fTemp320 * (1.0f - fTemp321) + fRec134[1] * fTemp321;
			fRec134[0] = ((std::fabs(fTemp322) > 1.1754944e-38f) ? fTemp322 : 0.0f);
			float fTemp323 = fTemp202 - fConst225 * (fConst226 * fRec137[2] + fConst227 * fRec137[1]);
			fRec137[0] = ((std::fabs(fTemp323) > 1.1754944e-38f) ? fTemp323 : 0.0f);
			float fTemp324 = std::fabs(fConst224 * (fRec137[0] - fRec137[2]));
			float fTemp325 = ((fTemp324 > fRec136[1]) ? fConst17 : fConst16);
			float fTemp326 = fTemp324 * (1.0f - fTemp325) + fRec136[1] * fTemp325;
			fRec136[0] = ((std::fabs(fTemp326) > 1.1754944e-38f) ? fTemp326 : 0.0f);
			float fTemp327 = fTemp202 - fConst232 * (fConst233 * fRec139[2] + fConst234 * fRec139[1]);
			fRec139[0] = ((std::fabs(fTemp327) > 1.1754944e-38f) ? fTemp327 : 0.0f);
			float fTemp328 = std::fabs(fConst231 * (fRec139[0] - fRec139[2]));
			float fTemp329 = ((fTemp328 > fRec138[1]) ? fConst17 : fConst16);
			float fTemp330 = fTemp328 * (1.0f - fTemp329) + fRec138[1] * fTemp329;
			fRec138[0] = ((std::fabs(fTemp330) > 1.1754944e-38f) ? fTemp330 : 0.0f);
			int iTemp331 = fRec136[0] > fRec138[0];
			float fTemp332 = ((iTemp331) ? fRec136[0] : fRec138[0]);
			int iTemp333 = fRec134[0] > fTemp332;
			float fTemp334 = ((iTemp333) ? fRec134[0] : fTemp332);
			int iTemp335 = fRec132[0] > fTemp334;
			float fTemp336 = ((iTemp335) ? fRec132[0] : fTemp334);
			int iTemp337 = fRec130[0] > fTemp336;
			float fTemp338 = ((iTemp337) ? fRec130[0] : fTemp336);
			int iTemp339 = fRec128[0] > fTemp338;
			float fTemp340 = ((iTemp339) ? fRec128[0] : fTemp338);
			int iTemp341 = fRec126[0] > fTemp340;
			float fTemp342 = ((iTemp341) ? fRec126[0] : fTemp340);
			int iTemp343 = fRec124[0] > fTemp342;
			float fTemp344 = ((iTemp343) ? fRec124[0] : fTemp342);
			int iTemp345 = fRec122[0] > fTemp344;
			float fTemp346 = ((iTemp345) ? fRec122[0] : fTemp344);
			int iTemp347 = fRec120[0] > fTemp346;
			float fTemp348 = ((iTemp347) ? fRec120[0] : fTemp346);
			int iTemp349 = fRec118[0] > fTemp348;
			float fTemp350 = ((iTemp349) ? fRec118[0] : fTemp348);
			int iTemp351 = fRec116[0] > fTemp350;
			float fTemp352 = ((iTemp351) ? fRec116[0] : fTemp350);
			int iTemp353 = fRec114[0] > fTemp352;
			float fTemp354 = ((iTemp353) ? fRec114[0] : fTemp352);
			int iTemp355 = fRec112[0] > fTemp354;
			float fTemp356 = ((iTemp355) ? fRec112[0] : fTemp354);
			int iTemp357 = fRec110[0] > fTemp356;
			float fTemp358 = ((iTemp357) ? fRec110[0] : fTemp356);
			int iTemp359 = fRec108[0] > fTemp358;
			float fTemp360 = ((iTemp359) ? fRec108[0] : fTemp358);
			int iTemp361 = fRec106[0] > fTemp360;
			float fTemp362 = ((iTemp361) ? fRec106[0] : fTemp360);
			int iTemp363 = fRec104[0] > fTemp362;
			float fTemp364 = ((iTemp363) ? fRec104[0] : fTemp362);
			int iTemp365 = fRec102[0] > fTemp364;
			float fTemp366 = ((iTemp365) ? fRec102[0] : fTemp364);
			int iTemp367 = fRec100[0] > fTemp366;
			float fTemp368 = ((iTemp367) ? fRec100[0] : fTemp366);
			int iTemp369 = fRec98[0] > fTemp368;
			float fTemp370 = ((iTemp369) ? fRec98[0] : fTemp368);
			int iTemp371 = fRec96[0] > fTemp370;
			float fTemp372 = ((iTemp371) ? fRec96[0] : fTemp370);
			int iTemp373 = fRec94[0] > fTemp372;
			float fTemp374 = ((iTemp373) ? fRec94[0] : fTemp372);
			int iTemp375 = fRec92[0] > fTemp374;
			float fTemp376 = ((iTemp375) ? fRec92[0] : fTemp374);
			int iTemp377 = fRec90[0] > fTemp376;
			float fTemp378 = ((iTemp377) ? fRec90[0] : fTemp376);
			int iTemp379 = fRec88[0] > fTemp378;
			float fTemp380 = ((iTemp379) ? fRec88[0] : fTemp378);
			int iTemp381 = fRec86[0] > fTemp380;
			float fTemp382 = ((iTemp381) ? fRec86[0] : fTemp380);
			int iTemp383 = fRec84[0] > fTemp382;
			float fTemp384 = ((iTemp383) ? fRec84[0] : fTemp382);
			int iTemp385 = fRec82[0] > fTemp384;
			float fTemp386 = ((iTemp385) ? fRec82[0] : fTemp384);
			int iTemp387 = fRec80[0] > fTemp386;
			float fTemp388 = ((iTemp387) ? fRec80[0] : fTemp386);
			int iTemp389 = fRec78[0] > fTemp388;
			float fTemp390 = ((iTemp389) ? fRec78[0] : fTemp388);
			int iTemp391 = fRec76[0] > fTemp390;
			float fTemp392 = static_cast<float>(std::abs((2e+01f * std::log10((1e-09f + ((iTemp391) ? fRec76[0] : fTemp390)) / (1e-09f + 0.03125f * (fRec138[0] + fRec136[0] + fRec134[0] + fRec132[0] + fRec130[0] + fRec128[0] + fRec126[0] + fRec124[0] + fRec122[0] + fRec120[0] + fRec118[0] + fRec116[0] + fRec114[0] + fRec112[0] + fRec110[0] + fRec108[0] + fRec106[0] + fRec104[0] + fRec102[0] + fRec100[0] + fRec98[0] + fRec96[0] + fRec94[0] + fRec92[0] + fRec90[0] + fRec88[0] + fRec86[0] + fRec84[0] + fRec82[0] + fRec80[0] + fRec76[0] + fRec78[0])))) > fSlow7));
			float fTemp393 = ((fTemp392 > fRec75[1]) ? fSlow13 : fSlow11);
			float fTemp394 = fTemp392 * (1.0f - fTemp393) + fRec75[1] * fTemp393;
			fRec75[0] = ((std::fabs(fTemp394) > 1.1754944e-38f) ? fTemp394 : 0.0f);
			float fTemp395 = fSlow14 * std::max<float>(0.0f, std::min<float>(1.0f, fRec75[0])) + fConst236 * fRec74[1];
			fRec74[0] = ((std::fabs(fTemp395) > 1.1754944e-38f) ? fTemp395 : 0.0f);
			float fTemp396 = fTemp202 * (1.0f - fRec74[0]);
			float fTemp397 = ((fRec75[0] > 0.5f) ? ((iTemp391) ? 6e+03f : ((iTemp389) ? 5326.8677f : ((iTemp387) ? 4729.253f : ((iTemp385) ? 4198.684f : ((iTemp383) ? 3727.639f : ((iTemp381) ? 3309.4397f : ((iTemp379) ? 2938.158f : ((iTemp377) ? 2608.5295f : ((iTemp375) ? 2315.8818f : ((iTemp373) ? 2056.066f : ((iTemp371) ? 1825.3986f : ((iTemp369) ? 1620.6094f : ((iTemp367) ? 1438.7953f : ((iTemp365) ? 1277.3787f : ((iTemp363) ? 1134.0712f : ((iTemp361) ? 1006.84106f : ((iTemp359) ? 893.8848f : ((iTemp357) ? 793.601f : ((iTemp355) ? 704.56793f : ((iTemp353) ? 625.5233f : ((iTemp351) ? 555.3467f : ((iTemp349) ? 493.043f : ((iTemp347) ? 437.72913f : ((iTemp345) ? 388.62085f : ((iTemp343) ? 345.02197f : ((iTemp341) ? 306.3144f : ((iTemp339) ? 271.94934f : ((iTemp337) ? 241.4397f : ((iTemp335) ? 214.35287f : ((iTemp333) ? 190.3049f : ((iTemp331) ? 168.95483f : 1.5e+02f))))))))))))))))))))))))))))))) : fRec142[1]);
			fRec142[0] = ((std::fabs(fTemp397) > 1.1754944e-38f) ? fTemp397 : 0.0f);
			float fTemp398 = fConst8 * std::min<float>(6e+03f, std::max<float>(1.5e+02f, fRec142[0])) + fConst7 * fRec141[1];
			fRec141[0] = ((std::fabs(fTemp398) > 1.1754944e-38f) ? fTemp398 : 0.0f);
			float fTemp399 = fRec140[1] * std::cos(fConst6 * fRec141[0]);
			float fTemp400 = fTemp201 + fConst5 * (fTemp200 + fTemp399) - fConst4 * fRec140[2];
			fRec140[0] = ((std::fabs(fTemp400) > 1.1754944e-38f) ? fTemp400 : 0.0f);
			float fTemp401 = (0.5f * fRec140[0] - fTemp399 + 0.5f * fRec140[2]) * fRec74[0];
			float fTemp402 = fConst5 * fTemp401 + fTemp396;
			float fTemp403 = fTemp402 - fConst13 * (fConst14 * fRec147[2] + fConst15 * fRec147[1]);
			fRec147[0] = ((std::fabs(fTemp403) > 1.1754944e-38f) ? fTemp403 : 0.0f);
			float fTemp404 = std::fabs(fConst12 * (fRec147[0] - fRec147[2]));
			float fTemp405 = ((fTemp404 > fRec146[1]) ? fConst17 : fConst16);
			float fTemp406 = fTemp404 * (1.0f - fTemp405) + fRec146[1] * fTemp405;
			fRec146[0] = ((std::fabs(fTemp406) > 1.1754944e-38f) ? fTemp406 : 0.0f);
			float fTemp407 = fTemp402 - fConst22 * (fConst23 * fRec149[2] + fConst24 * fRec149[1]);
			fRec149[0] = ((std::fabs(fTemp407) > 1.1754944e-38f) ? fTemp407 : 0.0f);
			float fTemp408 = std::fabs(fConst21 * (fRec149[0] - fRec149[2]));
			float fTemp409 = ((fTemp408 > fRec148[1]) ? fConst17 : fConst16);
			float fTemp410 = fTemp408 * (1.0f - fTemp409) + fRec148[1] * fTemp409;
			fRec148[0] = ((std::fabs(fTemp410) > 1.1754944e-38f) ? fTemp410 : 0.0f);
			float fTemp411 = fTemp402 - fConst29 * (fConst30 * fRec151[2] + fConst31 * fRec151[1]);
			fRec151[0] = ((std::fabs(fTemp411) > 1.1754944e-38f) ? fTemp411 : 0.0f);
			float fTemp412 = std::fabs(fConst28 * (fRec151[0] - fRec151[2]));
			float fTemp413 = ((fTemp412 > fRec150[1]) ? fConst17 : fConst16);
			float fTemp414 = fTemp412 * (1.0f - fTemp413) + fRec150[1] * fTemp413;
			fRec150[0] = ((std::fabs(fTemp414) > 1.1754944e-38f) ? fTemp414 : 0.0f);
			float fTemp415 = fTemp402 - fConst36 * (fConst37 * fRec153[2] + fConst38 * fRec153[1]);
			fRec153[0] = ((std::fabs(fTemp415) > 1.1754944e-38f) ? fTemp415 : 0.0f);
			float fTemp416 = std::fabs(fConst35 * (fRec153[0] - fRec153[2]));
			float fTemp417 = ((fTemp416 > fRec152[1]) ? fConst17 : fConst16);
			float fTemp418 = fTemp416 * (1.0f - fTemp417) + fRec152[1] * fTemp417;
			fRec152[0] = ((std::fabs(fTemp418) > 1.1754944e-38f) ? fTemp418 : 0.0f);
			float fTemp419 = fTemp402 - fConst43 * (fConst44 * fRec155[2] + fConst45 * fRec155[1]);
			fRec155[0] = ((std::fabs(fTemp419) > 1.1754944e-38f) ? fTemp419 : 0.0f);
			float fTemp420 = std::fabs(fConst42 * (fRec155[0] - fRec155[2]));
			float fTemp421 = ((fTemp420 > fRec154[1]) ? fConst17 : fConst16);
			float fTemp422 = fTemp420 * (1.0f - fTemp421) + fRec154[1] * fTemp421;
			fRec154[0] = ((std::fabs(fTemp422) > 1.1754944e-38f) ? fTemp422 : 0.0f);
			float fTemp423 = fTemp402 - fConst50 * (fConst51 * fRec157[2] + fConst52 * fRec157[1]);
			fRec157[0] = ((std::fabs(fTemp423) > 1.1754944e-38f) ? fTemp423 : 0.0f);
			float fTemp424 = std::fabs(fConst49 * (fRec157[0] - fRec157[2]));
			float fTemp425 = ((fTemp424 > fRec156[1]) ? fConst17 : fConst16);
			float fTemp426 = fTemp424 * (1.0f - fTemp425) + fRec156[1] * fTemp425;
			fRec156[0] = ((std::fabs(fTemp426) > 1.1754944e-38f) ? fTemp426 : 0.0f);
			float fTemp427 = fTemp402 - fConst57 * (fConst58 * fRec159[2] + fConst59 * fRec159[1]);
			fRec159[0] = ((std::fabs(fTemp427) > 1.1754944e-38f) ? fTemp427 : 0.0f);
			float fTemp428 = std::fabs(fConst56 * (fRec159[0] - fRec159[2]));
			float fTemp429 = ((fTemp428 > fRec158[1]) ? fConst17 : fConst16);
			float fTemp430 = fTemp428 * (1.0f - fTemp429) + fRec158[1] * fTemp429;
			fRec158[0] = ((std::fabs(fTemp430) > 1.1754944e-38f) ? fTemp430 : 0.0f);
			float fTemp431 = fTemp402 - fConst64 * (fConst65 * fRec161[2] + fConst66 * fRec161[1]);
			fRec161[0] = ((std::fabs(fTemp431) > 1.1754944e-38f) ? fTemp431 : 0.0f);
			float fTemp432 = std::fabs(fConst63 * (fRec161[0] - fRec161[2]));
			float fTemp433 = ((fTemp432 > fRec160[1]) ? fConst17 : fConst16);
			float fTemp434 = fTemp432 * (1.0f - fTemp433) + fRec160[1] * fTemp433;
			fRec160[0] = ((std::fabs(fTemp434) > 1.1754944e-38f) ? fTemp434 : 0.0f);
			float fTemp435 = fTemp402 - fConst71 * (fConst72 * fRec163[2] + fConst73 * fRec163[1]);
			fRec163[0] = ((std::fabs(fTemp435) > 1.1754944e-38f) ? fTemp435 : 0.0f);
			float fTemp436 = std::fabs(fConst70 * (fRec163[0] - fRec163[2]));
			float fTemp437 = ((fTemp436 > fRec162[1]) ? fConst17 : fConst16);
			float fTemp438 = fTemp436 * (1.0f - fTemp437) + fRec162[1] * fTemp437;
			fRec162[0] = ((std::fabs(fTemp438) > 1.1754944e-38f) ? fTemp438 : 0.0f);
			float fTemp439 = fTemp402 - fConst78 * (fConst79 * fRec165[2] + fConst80 * fRec165[1]);
			fRec165[0] = ((std::fabs(fTemp439) > 1.1754944e-38f) ? fTemp439 : 0.0f);
			float fTemp440 = std::fabs(fConst77 * (fRec165[0] - fRec165[2]));
			float fTemp441 = ((fTemp440 > fRec164[1]) ? fConst17 : fConst16);
			float fTemp442 = fTemp440 * (1.0f - fTemp441) + fRec164[1] * fTemp441;
			fRec164[0] = ((std::fabs(fTemp442) > 1.1754944e-38f) ? fTemp442 : 0.0f);
			float fTemp443 = fTemp402 - fConst85 * (fConst86 * fRec167[2] + fConst87 * fRec167[1]);
			fRec167[0] = ((std::fabs(fTemp443) > 1.1754944e-38f) ? fTemp443 : 0.0f);
			float fTemp444 = std::fabs(fConst84 * (fRec167[0] - fRec167[2]));
			float fTemp445 = ((fTemp444 > fRec166[1]) ? fConst17 : fConst16);
			float fTemp446 = fTemp444 * (1.0f - fTemp445) + fRec166[1] * fTemp445;
			fRec166[0] = ((std::fabs(fTemp446) > 1.1754944e-38f) ? fTemp446 : 0.0f);
			float fTemp447 = fTemp402 - fConst92 * (fConst93 * fRec169[2] + fConst94 * fRec169[1]);
			fRec169[0] = ((std::fabs(fTemp447) > 1.1754944e-38f) ? fTemp447 : 0.0f);
			float fTemp448 = std::fabs(fConst91 * (fRec169[0] - fRec169[2]));
			float fTemp449 = ((fTemp448 > fRec168[1]) ? fConst17 : fConst16);
			float fTemp450 = fTemp448 * (1.0f - fTemp449) + fRec168[1] * fTemp449;
			fRec168[0] = ((std::fabs(fTemp450) > 1.1754944e-38f) ? fTemp450 : 0.0f);
			float fTemp451 = fTemp402 - fConst99 * (fConst100 * fRec171[2] + fConst101 * fRec171[1]);
			fRec171[0] = ((std::fabs(fTemp451) > 1.1754944e-38f) ? fTemp451 : 0.0f);
			float fTemp452 = std::fabs(fConst98 * (fRec171[0] - fRec171[2]));
			float fTemp453 = ((fTemp452 > fRec170[1]) ? fConst17 : fConst16);
			float fTemp454 = fTemp452 * (1.0f - fTemp453) + fRec170[1] * fTemp453;
			fRec170[0] = ((std::fabs(fTemp454) > 1.1754944e-38f) ? fTemp454 : 0.0f);
			float fTemp455 = fTemp402 - fConst106 * (fConst107 * fRec173[2] + fConst108 * fRec173[1]);
			fRec173[0] = ((std::fabs(fTemp455) > 1.1754944e-38f) ? fTemp455 : 0.0f);
			float fTemp456 = std::fabs(fConst105 * (fRec173[0] - fRec173[2]));
			float fTemp457 = ((fTemp456 > fRec172[1]) ? fConst17 : fConst16);
			float fTemp458 = fTemp456 * (1.0f - fTemp457) + fRec172[1] * fTemp457;
			fRec172[0] = ((std::fabs(fTemp458) > 1.1754944e-38f) ? fTemp458 : 0.0f);
			float fTemp459 = fTemp402 - fConst113 * (fConst114 * fRec175[2] + fConst115 * fRec175[1]);
			fRec175[0] = ((std::fabs(fTemp459) > 1.1754944e-38f) ? fTemp459 : 0.0f);
			float fTemp460 = std::fabs(fConst112 * (fRec175[0] - fRec175[2]));
			float fTemp461 = ((fTemp460 > fRec174[1]) ? fConst17 : fConst16);
			float fTemp462 = fTemp460 * (1.0f - fTemp461) + fRec174[1] * fTemp461;
			fRec174[0] = ((std::fabs(fTemp462) > 1.1754944e-38f) ? fTemp462 : 0.0f);
			float fTemp463 = fTemp402 - fConst120 * (fConst121 * fRec177[2] + fConst122 * fRec177[1]);
			fRec177[0] = ((std::fabs(fTemp463) > 1.1754944e-38f) ? fTemp463 : 0.0f);
			float fTemp464 = std::fabs(fConst119 * (fRec177[0] - fRec177[2]));
			float fTemp465 = ((fTemp464 > fRec176[1]) ? fConst17 : fConst16);
			float fTemp466 = fTemp464 * (1.0f - fTemp465) + fRec176[1] * fTemp465;
			fRec176[0] = ((std::fabs(fTemp466) > 1.1754944e-38f) ? fTemp466 : 0.0f);
			float fTemp467 = fTemp402 - fConst127 * (fConst128 * fRec179[2] + fConst129 * fRec179[1]);
			fRec179[0] = ((std::fabs(fTemp467) > 1.1754944e-38f) ? fTemp467 : 0.0f);
			float fTemp468 = std::fabs(fConst126 * (fRec179[0] - fRec179[2]));
			float fTemp469 = ((fTemp468 > fRec178[1]) ? fConst17 : fConst16);
			float fTemp470 = fTemp468 * (1.0f - fTemp469) + fRec178[1] * fTemp469;
			fRec178[0] = ((std::fabs(fTemp470) > 1.1754944e-38f) ? fTemp470 : 0.0f);
			float fTemp471 = fTemp402 - fConst134 * (fConst135 * fRec181[2] + fConst136 * fRec181[1]);
			fRec181[0] = ((std::fabs(fTemp471) > 1.1754944e-38f) ? fTemp471 : 0.0f);
			float fTemp472 = std::fabs(fConst133 * (fRec181[0] - fRec181[2]));
			float fTemp473 = ((fTemp472 > fRec180[1]) ? fConst17 : fConst16);
			float fTemp474 = fTemp472 * (1.0f - fTemp473) + fRec180[1] * fTemp473;
			fRec180[0] = ((std::fabs(fTemp474) > 1.1754944e-38f) ? fTemp474 : 0.0f);
			float fTemp475 = fTemp402 - fConst141 * (fConst142 * fRec183[2] + fConst143 * fRec183[1]);
			fRec183[0] = ((std::fabs(fTemp475) > 1.1754944e-38f) ? fTemp475 : 0.0f);
			float fTemp476 = std::fabs(fConst140 * (fRec183[0] - fRec183[2]));
			float fTemp477 = ((fTemp476 > fRec182[1]) ? fConst17 : fConst16);
			float fTemp478 = fTemp476 * (1.0f - fTemp477) + fRec182[1] * fTemp477;
			fRec182[0] = ((std::fabs(fTemp478) > 1.1754944e-38f) ? fTemp478 : 0.0f);
			float fTemp479 = fTemp402 - fConst148 * (fConst149 * fRec185[2] + fConst150 * fRec185[1]);
			fRec185[0] = ((std::fabs(fTemp479) > 1.1754944e-38f) ? fTemp479 : 0.0f);
			float fTemp480 = std::fabs(fConst147 * (fRec185[0] - fRec185[2]));
			float fTemp481 = ((fTemp480 > fRec184[1]) ? fConst17 : fConst16);
			float fTemp482 = fTemp480 * (1.0f - fTemp481) + fRec184[1] * fTemp481;
			fRec184[0] = ((std::fabs(fTemp482) > 1.1754944e-38f) ? fTemp482 : 0.0f);
			float fTemp483 = fTemp402 - fConst155 * (fConst156 * fRec187[2] + fConst157 * fRec187[1]);
			fRec187[0] = ((std::fabs(fTemp483) > 1.1754944e-38f) ? fTemp483 : 0.0f);
			float fTemp484 = std::fabs(fConst154 * (fRec187[0] - fRec187[2]));
			float fTemp485 = ((fTemp484 > fRec186[1]) ? fConst17 : fConst16);
			float fTemp486 = fTemp484 * (1.0f - fTemp485) + fRec186[1] * fTemp485;
			fRec186[0] = ((std::fabs(fTemp486) > 1.1754944e-38f) ? fTemp486 : 0.0f);
			float fTemp487 = fTemp402 - fConst162 * (fConst163 * fRec189[2] + fConst164 * fRec189[1]);
			fRec189[0] = ((std::fabs(fTemp487) > 1.1754944e-38f) ? fTemp487 : 0.0f);
			float fTemp488 = std::fabs(fConst161 * (fRec189[0] - fRec189[2]));
			float fTemp489 = ((fTemp488 > fRec188[1]) ? fConst17 : fConst16);
			float fTemp490 = fTemp488 * (1.0f - fTemp489) + fRec188[1] * fTemp489;
			fRec188[0] = ((std::fabs(fTemp490) > 1.1754944e-38f) ? fTemp490 : 0.0f);
			float fTemp491 = fTemp402 - fConst169 * (fConst170 * fRec191[2] + fConst171 * fRec191[1]);
			fRec191[0] = ((std::fabs(fTemp491) > 1.1754944e-38f) ? fTemp491 : 0.0f);
			float fTemp492 = std::fabs(fConst168 * (fRec191[0] - fRec191[2]));
			float fTemp493 = ((fTemp492 > fRec190[1]) ? fConst17 : fConst16);
			float fTemp494 = fTemp492 * (1.0f - fTemp493) + fRec190[1] * fTemp493;
			fRec190[0] = ((std::fabs(fTemp494) > 1.1754944e-38f) ? fTemp494 : 0.0f);
			float fTemp495 = fTemp402 - fConst176 * (fConst177 * fRec193[2] + fConst178 * fRec193[1]);
			fRec193[0] = ((std::fabs(fTemp495) > 1.1754944e-38f) ? fTemp495 : 0.0f);
			float fTemp496 = std::fabs(fConst175 * (fRec193[0] - fRec193[2]));
			float fTemp497 = ((fTemp496 > fRec192[1]) ? fConst17 : fConst16);
			float fTemp498 = fTemp496 * (1.0f - fTemp497) + fRec192[1] * fTemp497;
			fRec192[0] = ((std::fabs(fTemp498) > 1.1754944e-38f) ? fTemp498 : 0.0f);
			float fTemp499 = fTemp402 - fConst183 * (fConst184 * fRec195[2] + fConst185 * fRec195[1]);
			fRec195[0] = ((std::fabs(fTemp499) > 1.1754944e-38f) ? fTemp499 : 0.0f);
			float fTemp500 = std::fabs(fConst182 * (fRec195[0] - fRec195[2]));
			float fTemp501 = ((fTemp500 > fRec194[1]) ? fConst17 : fConst16);
			float fTemp502 = fTemp500 * (1.0f - fTemp501) + fRec194[1] * fTemp501;
			fRec194[0] = ((std::fabs(fTemp502) > 1.1754944e-38f) ? fTemp502 : 0.0f);
			float fTemp503 = fTemp402 - fConst190 * (fConst191 * fRec197[2] + fConst192 * fRec197[1]);
			fRec197[0] = ((std::fabs(fTemp503) > 1.1754944e-38f) ? fTemp503 : 0.0f);
			float fTemp504 = std::fabs(fConst189 * (fRec197[0] - fRec197[2]));
			float fTemp505 = ((fTemp504 > fRec196[1]) ? fConst17 : fConst16);
			float fTemp506 = fTemp504 * (1.0f - fTemp505) + fRec196[1] * fTemp505;
			fRec196[0] = ((std::fabs(fTemp506) > 1.1754944e-38f) ? fTemp506 : 0.0f);
			float fTemp507 = fTemp402 - fConst197 * (fConst198 * fRec199[2] + fConst199 * fRec199[1]);
			fRec199[0] = ((std::fabs(fTemp507) > 1.1754944e-38f) ? fTemp507 : 0.0f);
			float fTemp508 = std::fabs(fConst196 * (fRec199[0] - fRec199[2]));
			float fTemp509 = ((fTemp508 > fRec198[1]) ? fConst17 : fConst16);
			float fTemp510 = fTemp508 * (1.0f - fTemp509) + fRec198[1] * fTemp509;
			fRec198[0] = ((std::fabs(fTemp510) > 1.1754944e-38f) ? fTemp510 : 0.0f);
			float fTemp511 = fTemp402 - fConst204 * (fConst205 * fRec201[2] + fConst206 * fRec201[1]);
			fRec201[0] = ((std::fabs(fTemp511) > 1.1754944e-38f) ? fTemp511 : 0.0f);
			float fTemp512 = std::fabs(fConst203 * (fRec201[0] - fRec201[2]));
			float fTemp513 = ((fTemp512 > fRec200[1]) ? fConst17 : fConst16);
			float fTemp514 = fTemp512 * (1.0f - fTemp513) + fRec200[1] * fTemp513;
			fRec200[0] = ((std::fabs(fTemp514) > 1.1754944e-38f) ? fTemp514 : 0.0f);
			float fTemp515 = fTemp402 - fConst211 * (fConst212 * fRec203[2] + fConst213 * fRec203[1]);
			fRec203[0] = ((std::fabs(fTemp515) > 1.1754944e-38f) ? fTemp515 : 0.0f);
			float fTemp516 = std::fabs(fConst210 * (fRec203[0] - fRec203[2]));
			float fTemp517 = ((fTemp516 > fRec202[1]) ? fConst17 : fConst16);
			float fTemp518 = fTemp516 * (1.0f - fTemp517) + fRec202[1] * fTemp517;
			fRec202[0] = ((std::fabs(fTemp518) > 1.1754944e-38f) ? fTemp518 : 0.0f);
			float fTemp519 = fTemp402 - fConst218 * (fConst219 * fRec205[2] + fConst220 * fRec205[1]);
			fRec205[0] = ((std::fabs(fTemp519) > 1.1754944e-38f) ? fTemp519 : 0.0f);
			float fTemp520 = std::fabs(fConst217 * (fRec205[0] - fRec205[2]));
			float fTemp521 = ((fTemp520 > fRec204[1]) ? fConst17 : fConst16);
			float fTemp522 = fTemp520 * (1.0f - fTemp521) + fRec204[1] * fTemp521;
			fRec204[0] = ((std::fabs(fTemp522) > 1.1754944e-38f) ? fTemp522 : 0.0f);
			float fTemp523 = fTemp402 - fConst225 * (fConst226 * fRec207[2] + fConst227 * fRec207[1]);
			fRec207[0] = ((std::fabs(fTemp523) > 1.1754944e-38f) ? fTemp523 : 0.0f);
			float fTemp524 = std::fabs(fConst224 * (fRec207[0] - fRec207[2]));
			float fTemp525 = ((fTemp524 > fRec206[1]) ? fConst17 : fConst16);
			float fTemp526 = fTemp524 * (1.0f - fTemp525) + fRec206[1] * fTemp525;
			fRec206[0] = ((std::fabs(fTemp526) > 1.1754944e-38f) ? fTemp526 : 0.0f);
			float fTemp527 = fTemp402 - fConst232 * (fConst233 * fRec209[2] + fConst234 * fRec209[1]);
			fRec209[0] = ((std::fabs(fTemp527) > 1.1754944e-38f) ? fTemp527 : 0.0f);
			float fTemp528 = std::fabs(fConst231 * (fRec209[0] - fRec209[2]));
			float fTemp529 = ((fTemp528 > fRec208[1]) ? fConst17 : fConst16);
			float fTemp530 = fTemp528 * (1.0f - fTemp529) + fRec208[1] * fTemp529;
			fRec208[0] = ((std::fabs(fTemp530) > 1.1754944e-38f) ? fTemp530 : 0.0f);
			int iTemp531 = fRec206[0] > fRec208[0];
			float fTemp532 = ((iTemp531) ? fRec206[0] : fRec208[0]);
			int iTemp533 = fRec204[0] > fTemp532;
			float fTemp534 = ((iTemp533) ? fRec204[0] : fTemp532);
			int iTemp535 = fRec202[0] > fTemp534;
			float fTemp536 = ((iTemp535) ? fRec202[0] : fTemp534);
			int iTemp537 = fRec200[0] > fTemp536;
			float fTemp538 = ((iTemp537) ? fRec200[0] : fTemp536);
			int iTemp539 = fRec198[0] > fTemp538;
			float fTemp540 = ((iTemp539) ? fRec198[0] : fTemp538);
			int iTemp541 = fRec196[0] > fTemp540;
			float fTemp542 = ((iTemp541) ? fRec196[0] : fTemp540);
			int iTemp543 = fRec194[0] > fTemp542;
			float fTemp544 = ((iTemp543) ? fRec194[0] : fTemp542);
			int iTemp545 = fRec192[0] > fTemp544;
			float fTemp546 = ((iTemp545) ? fRec192[0] : fTemp544);
			int iTemp547 = fRec190[0] > fTemp546;
			float fTemp548 = ((iTemp547) ? fRec190[0] : fTemp546);
			int iTemp549 = fRec188[0] > fTemp548;
			float fTemp550 = ((iTemp549) ? fRec188[0] : fTemp548);
			int iTemp551 = fRec186[0] > fTemp550;
			float fTemp552 = ((iTemp551) ? fRec186[0] : fTemp550);
			int iTemp553 = fRec184[0] > fTemp552;
			float fTemp554 = ((iTemp553) ? fRec184[0] : fTemp552);
			int iTemp555 = fRec182[0] > fTemp554;
			float fTemp556 = ((iTemp555) ? fRec182[0] : fTemp554);
			int iTemp557 = fRec180[0] > fTemp556;
			float fTemp558 = ((iTemp557) ? fRec180[0] : fTemp556);
			int iTemp559 = fRec178[0] > fTemp558;
			float fTemp560 = ((iTemp559) ? fRec178[0] : fTemp558);
			int iTemp561 = fRec176[0] > fTemp560;
			float fTemp562 = ((iTemp561) ? fRec176[0] : fTemp560);
			int iTemp563 = fRec174[0] > fTemp562;
			float fTemp564 = ((iTemp563) ? fRec174[0] : fTemp562);
			int iTemp565 = fRec172[0] > fTemp564;
			float fTemp566 = ((iTemp565) ? fRec172[0] : fTemp564);
			int iTemp567 = fRec170[0] > fTemp566;
			float fTemp568 = ((iTemp567) ? fRec170[0] : fTemp566);
			int iTemp569 = fRec168[0] > fTemp568;
			float fTemp570 = ((iTemp569) ? fRec168[0] : fTemp568);
			int iTemp571 = fRec166[0] > fTemp570;
			float fTemp572 = ((iTemp571) ? fRec166[0] : fTemp570);
			int iTemp573 = fRec164[0] > fTemp572;
			float fTemp574 = ((iTemp573) ? fRec164[0] : fTemp572);
			int iTemp575 = fRec162[0] > fTemp574;
			float fTemp576 = ((iTemp575) ? fRec162[0] : fTemp574);
			int iTemp577 = fRec160[0] > fTemp576;
			float fTemp578 = ((iTemp577) ? fRec160[0] : fTemp576);
			int iTemp579 = fRec158[0] > fTemp578;
			float fTemp580 = ((iTemp579) ? fRec158[0] : fTemp578);
			int iTemp581 = fRec156[0] > fTemp580;
			float fTemp582 = ((iTemp581) ? fRec156[0] : fTemp580);
			int iTemp583 = fRec154[0] > fTemp582;
			float fTemp584 = ((iTemp583) ? fRec154[0] : fTemp582);
			int iTemp585 = fRec152[0] > fTemp584;
			float fTemp586 = ((iTemp585) ? fRec152[0] : fTemp584);
			int iTemp587 = fRec150[0] > fTemp586;
			float fTemp588 = ((iTemp587) ? fRec150[0] : fTemp586);
			int iTemp589 = fRec148[0] > fTemp588;
			float fTemp590 = ((iTemp589) ? fRec148[0] : fTemp588);
			int iTemp591 = fRec146[0] > fTemp590;
			float fTemp592 = static_cast<float>(std::abs((2e+01f * std::log10((1e-09f + ((iTemp591) ? fRec146[0] : fTemp590)) / (1e-09f + 0.03125f * (fRec208[0] + fRec206[0] + fRec204[0] + fRec202[0] + fRec200[0] + fRec198[0] + fRec196[0] + fRec194[0] + fRec192[0] + fRec190[0] + fRec188[0] + fRec186[0] + fRec184[0] + fRec182[0] + fRec180[0] + fRec178[0] + fRec176[0] + fRec174[0] + fRec172[0] + fRec170[0] + fRec168[0] + fRec166[0] + fRec164[0] + fRec162[0] + fRec160[0] + fRec158[0] + fRec156[0] + fRec154[0] + fRec152[0] + fRec150[0] + fRec146[0] + fRec148[0])))) > fSlow7));
			float fTemp593 = ((fTemp592 > fRec145[1]) ? fSlow13 : fSlow11);
			float fTemp594 = fTemp592 * (1.0f - fTemp593) + fRec145[1] * fTemp593;
			fRec145[0] = ((std::fabs(fTemp594) > 1.1754944e-38f) ? fTemp594 : 0.0f);
			float fTemp595 = ((fRec145[0] > 0.5f) ? ((iTemp591) ? 6e+03f : ((iTemp589) ? 5326.8677f : ((iTemp587) ? 4729.253f : ((iTemp585) ? 4198.684f : ((iTemp583) ? 3727.639f : ((iTemp581) ? 3309.4397f : ((iTemp579) ? 2938.158f : ((iTemp577) ? 2608.5295f : ((iTemp575) ? 2315.8818f : ((iTemp573) ? 2056.066f : ((iTemp571) ? 1825.3986f : ((iTemp569) ? 1620.6094f : ((iTemp567) ? 1438.7953f : ((iTemp565) ? 1277.3787f : ((iTemp563) ? 1134.0712f : ((iTemp561) ? 1006.84106f : ((iTemp559) ? 893.8848f : ((iTemp557) ? 793.601f : ((iTemp555) ? 704.56793f : ((iTemp553) ? 625.5233f : ((iTemp551) ? 555.3467f : ((iTemp549) ? 493.043f : ((iTemp547) ? 437.72913f : ((iTemp545) ? 388.62085f : ((iTemp543) ? 345.02197f : ((iTemp541) ? 306.3144f : ((iTemp539) ? 271.94934f : ((iTemp537) ? 241.4397f : ((iTemp535) ? 214.35287f : ((iTemp533) ? 190.3049f : ((iTemp531) ? 168.95483f : 1.5e+02f))))))))))))))))))))))))))))))) : fRec144[1]);
			fRec144[0] = ((std::fabs(fTemp595) > 1.1754944e-38f) ? fTemp595 : 0.0f);
			float fTemp596 = fConst8 * std::min<float>(6e+03f, std::max<float>(1.5e+02f, fRec144[0])) + fConst7 * fRec143[1];
			fRec143[0] = ((std::fabs(fTemp596) > 1.1754944e-38f) ? fTemp596 : 0.0f);
			float fTemp597 = fRec4[1] * std::cos(fConst6 * fRec143[0]);
			float fTemp598 = fTemp396 + fConst5 * (fTemp401 + fTemp597) - fConst4 * fRec4[2];
			fRec4[0] = ((std::fabs(fTemp598) > 1.1754944e-38f) ? fTemp598 : 0.0f);
			float fTemp599 = fSlow14 * std::max<float>(0.0f, std::min<float>(1.0f, fRec145[0])) + fConst236 * fRec210[1];
			fRec210[0] = ((std::fabs(fTemp599) > 1.1754944e-38f) ? fTemp599 : 0.0f);
			float fTemp600 = ((iSlow6) ? fTemp2 : fConst5 * (0.5f * fRec4[0] - fTemp597 + 0.5f * fRec4[2]) * fRec210[0] + fTemp402 * (1.0f - fRec210[0]));
			float fTemp601 = fTemp1 * fTemp600 - fSlow5 * (fSlow15 * fRec3[2] + fSlow16 * fRec3[1]);
			fRec3[0] = ((std::fabs(fTemp601) > 1.1754944e-38f) ? fTemp601 : 0.0f);
			float fTemp602 = fSlow5 * (fRec3[2] + fRec3[0] + 2.0f * fRec3[1]) - fSlow4 * (fSlow17 * fRec2[2] + fSlow16 * fRec2[1]);
			fRec2[0] = ((std::fabs(fTemp602) > 1.1754944e-38f) ? fTemp602 : 0.0f);
			float fTemp603 = fSlow4 * (fRec2[2] + fRec2[0] + 2.0f * fRec2[1]) - fSlow18 * (fSlow19 * fRec1[2] + fSlow16 * fRec1[1]);
			fRec1[0] = ((std::fabs(fTemp603) > 1.1754944e-38f) ? fTemp603 : 0.0f);
			float fTemp604 = fRec1[2] + fRec1[0] + 2.0f * fRec1[1];
			float fTemp605 = static_cast<float>(iRec0[1]);
			float fTemp606 = fTemp600 * static_cast<float>(-iRec0[1]) - fSlow5 * (fSlow15 * fRec213[2] + fSlow16 * fRec213[1]);
			fRec213[0] = ((std::fabs(fTemp606) > 1.1754944e-38f) ? fTemp606 : 0.0f);
			float fTemp607 = fSlow5 * (fRec213[2] + fRec213[0] + 2.0f * fRec213[1]) - fSlow4 * (fSlow17 * fRec212[2] + fSlow16 * fRec212[1]);
			fRec212[0] = ((std::fabs(fTemp607) > 1.1754944e-38f) ? fTemp607 : 0.0f);
			float fTemp608 = fSlow4 * (fRec212[2] + fRec212[0] + 2.0f * fRec212[1]) - fSlow18 * (fSlow19 * fRec211[2] + fSlow16 * fRec211[1]);
			fRec211[0] = ((std::fabs(fTemp608) > 1.1754944e-38f) ? fTemp608 : 0.0f);
			float fTemp609 = fRec211[2] + fRec211[0] + 2.0f * fRec211[1];
			float fTemp610 = ((iTemp0) ? 0.0f : fSlow20 + fRec215[1]);
			float fTemp611 = fTemp610 - std::floor(fTemp610);
			fRec215[0] = ((std::fabs(fTemp611) > 1.1754944e-38f) ? fTemp611 : 0.0f);
			int iTemp612 = std::max<int>(0, std::min<int>(static_cast<int>(65536.0f * fRec215[0]), 65535));
			output0[i0] = static_cast<FAUSTFLOAT>(fSlow3 * ((fTemp1 * fTemp604 - fTemp605 * fTemp609) * ftbl0icc_suppressor_f32SIG0[iTemp612] - (fTemp1 * fTemp609 + fTemp604 * fTemp605) * ftbl1icc_suppressor_f32SIG1[iTemp612]));
			iVec0[1] = iVec0[0];
			iRec0[2] = iRec0[1];
			iRec0[1] = iRec0[0];
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
			fRec66[2] = fRec66[1];
			fRec66[1] = fRec66[0];
			fRec65[1] = fRec65[0];
			fRec68[2] = fRec68[1];
			fRec68[1] = fRec68[0];
			fRec67[1] = fRec67[0];
			fRec70[2] = fRec70[1];
			fRec70[1] = fRec70[0];
			fRec69[1] = fRec69[0];
			fRec72[2] = fRec72[1];
			fRec72[1] = fRec72[0];
			fRec71[1] = fRec71[0];
			fRec8[1] = fRec8[0];
			fRec7[1] = fRec7[0];
			fRec6[1] = fRec6[0];
			fRec5[2] = fRec5[1];
			fRec5[1] = fRec5[0];
			fRec73[1] = fRec73[0];
			fRec77[2] = fRec77[1];
			fRec77[1] = fRec77[0];
			fRec76[1] = fRec76[0];
			fRec79[2] = fRec79[1];
			fRec79[1] = fRec79[0];
			fRec78[1] = fRec78[0];
			fRec81[2] = fRec81[1];
			fRec81[1] = fRec81[0];
			fRec80[1] = fRec80[0];
			fRec83[2] = fRec83[1];
			fRec83[1] = fRec83[0];
			fRec82[1] = fRec82[0];
			fRec85[2] = fRec85[1];
			fRec85[1] = fRec85[0];
			fRec84[1] = fRec84[0];
			fRec87[2] = fRec87[1];
			fRec87[1] = fRec87[0];
			fRec86[1] = fRec86[0];
			fRec89[2] = fRec89[1];
			fRec89[1] = fRec89[0];
			fRec88[1] = fRec88[0];
			fRec91[2] = fRec91[1];
			fRec91[1] = fRec91[0];
			fRec90[1] = fRec90[0];
			fRec93[2] = fRec93[1];
			fRec93[1] = fRec93[0];
			fRec92[1] = fRec92[0];
			fRec95[2] = fRec95[1];
			fRec95[1] = fRec95[0];
			fRec94[1] = fRec94[0];
			fRec97[2] = fRec97[1];
			fRec97[1] = fRec97[0];
			fRec96[1] = fRec96[0];
			fRec99[2] = fRec99[1];
			fRec99[1] = fRec99[0];
			fRec98[1] = fRec98[0];
			fRec101[2] = fRec101[1];
			fRec101[1] = fRec101[0];
			fRec100[1] = fRec100[0];
			fRec103[2] = fRec103[1];
			fRec103[1] = fRec103[0];
			fRec102[1] = fRec102[0];
			fRec105[2] = fRec105[1];
			fRec105[1] = fRec105[0];
			fRec104[1] = fRec104[0];
			fRec107[2] = fRec107[1];
			fRec107[1] = fRec107[0];
			fRec106[1] = fRec106[0];
			fRec109[2] = fRec109[1];
			fRec109[1] = fRec109[0];
			fRec108[1] = fRec108[0];
			fRec111[2] = fRec111[1];
			fRec111[1] = fRec111[0];
			fRec110[1] = fRec110[0];
			fRec113[2] = fRec113[1];
			fRec113[1] = fRec113[0];
			fRec112[1] = fRec112[0];
			fRec115[2] = fRec115[1];
			fRec115[1] = fRec115[0];
			fRec114[1] = fRec114[0];
			fRec117[2] = fRec117[1];
			fRec117[1] = fRec117[0];
			fRec116[1] = fRec116[0];
			fRec119[2] = fRec119[1];
			fRec119[1] = fRec119[0];
			fRec118[1] = fRec118[0];
			fRec121[2] = fRec121[1];
			fRec121[1] = fRec121[0];
			fRec120[1] = fRec120[0];
			fRec123[2] = fRec123[1];
			fRec123[1] = fRec123[0];
			fRec122[1] = fRec122[0];
			fRec125[2] = fRec125[1];
			fRec125[1] = fRec125[0];
			fRec124[1] = fRec124[0];
			fRec127[2] = fRec127[1];
			fRec127[1] = fRec127[0];
			fRec126[1] = fRec126[0];
			fRec129[2] = fRec129[1];
			fRec129[1] = fRec129[0];
			fRec128[1] = fRec128[0];
			fRec131[2] = fRec131[1];
			fRec131[1] = fRec131[0];
			fRec130[1] = fRec130[0];
			fRec133[2] = fRec133[1];
			fRec133[1] = fRec133[0];
			fRec132[1] = fRec132[0];
			fRec135[2] = fRec135[1];
			fRec135[1] = fRec135[0];
			fRec134[1] = fRec134[0];
			fRec137[2] = fRec137[1];
			fRec137[1] = fRec137[0];
			fRec136[1] = fRec136[0];
			fRec139[2] = fRec139[1];
			fRec139[1] = fRec139[0];
			fRec138[1] = fRec138[0];
			fRec75[1] = fRec75[0];
			fRec74[1] = fRec74[0];
			fRec142[1] = fRec142[0];
			fRec141[1] = fRec141[0];
			fRec140[2] = fRec140[1];
			fRec140[1] = fRec140[0];
			fRec147[2] = fRec147[1];
			fRec147[1] = fRec147[0];
			fRec146[1] = fRec146[0];
			fRec149[2] = fRec149[1];
			fRec149[1] = fRec149[0];
			fRec148[1] = fRec148[0];
			fRec151[2] = fRec151[1];
			fRec151[1] = fRec151[0];
			fRec150[1] = fRec150[0];
			fRec153[2] = fRec153[1];
			fRec153[1] = fRec153[0];
			fRec152[1] = fRec152[0];
			fRec155[2] = fRec155[1];
			fRec155[1] = fRec155[0];
			fRec154[1] = fRec154[0];
			fRec157[2] = fRec157[1];
			fRec157[1] = fRec157[0];
			fRec156[1] = fRec156[0];
			fRec159[2] = fRec159[1];
			fRec159[1] = fRec159[0];
			fRec158[1] = fRec158[0];
			fRec161[2] = fRec161[1];
			fRec161[1] = fRec161[0];
			fRec160[1] = fRec160[0];
			fRec163[2] = fRec163[1];
			fRec163[1] = fRec163[0];
			fRec162[1] = fRec162[0];
			fRec165[2] = fRec165[1];
			fRec165[1] = fRec165[0];
			fRec164[1] = fRec164[0];
			fRec167[2] = fRec167[1];
			fRec167[1] = fRec167[0];
			fRec166[1] = fRec166[0];
			fRec169[2] = fRec169[1];
			fRec169[1] = fRec169[0];
			fRec168[1] = fRec168[0];
			fRec171[2] = fRec171[1];
			fRec171[1] = fRec171[0];
			fRec170[1] = fRec170[0];
			fRec173[2] = fRec173[1];
			fRec173[1] = fRec173[0];
			fRec172[1] = fRec172[0];
			fRec175[2] = fRec175[1];
			fRec175[1] = fRec175[0];
			fRec174[1] = fRec174[0];
			fRec177[2] = fRec177[1];
			fRec177[1] = fRec177[0];
			fRec176[1] = fRec176[0];
			fRec179[2] = fRec179[1];
			fRec179[1] = fRec179[0];
			fRec178[1] = fRec178[0];
			fRec181[2] = fRec181[1];
			fRec181[1] = fRec181[0];
			fRec180[1] = fRec180[0];
			fRec183[2] = fRec183[1];
			fRec183[1] = fRec183[0];
			fRec182[1] = fRec182[0];
			fRec185[2] = fRec185[1];
			fRec185[1] = fRec185[0];
			fRec184[1] = fRec184[0];
			fRec187[2] = fRec187[1];
			fRec187[1] = fRec187[0];
			fRec186[1] = fRec186[0];
			fRec189[2] = fRec189[1];
			fRec189[1] = fRec189[0];
			fRec188[1] = fRec188[0];
			fRec191[2] = fRec191[1];
			fRec191[1] = fRec191[0];
			fRec190[1] = fRec190[0];
			fRec193[2] = fRec193[1];
			fRec193[1] = fRec193[0];
			fRec192[1] = fRec192[0];
			fRec195[2] = fRec195[1];
			fRec195[1] = fRec195[0];
			fRec194[1] = fRec194[0];
			fRec197[2] = fRec197[1];
			fRec197[1] = fRec197[0];
			fRec196[1] = fRec196[0];
			fRec199[2] = fRec199[1];
			fRec199[1] = fRec199[0];
			fRec198[1] = fRec198[0];
			fRec201[2] = fRec201[1];
			fRec201[1] = fRec201[0];
			fRec200[1] = fRec200[0];
			fRec203[2] = fRec203[1];
			fRec203[1] = fRec203[0];
			fRec202[1] = fRec202[0];
			fRec205[2] = fRec205[1];
			fRec205[1] = fRec205[0];
			fRec204[1] = fRec204[0];
			fRec207[2] = fRec207[1];
			fRec207[1] = fRec207[0];
			fRec206[1] = fRec206[0];
			fRec209[2] = fRec209[1];
			fRec209[1] = fRec209[0];
			fRec208[1] = fRec208[0];
			fRec145[1] = fRec145[0];
			fRec144[1] = fRec144[0];
			fRec143[1] = fRec143[0];
			fRec4[2] = fRec4[1];
			fRec4[1] = fRec4[0];
			fRec210[1] = fRec210[0];
			fRec3[2] = fRec3[1];
			fRec3[1] = fRec3[0];
			fRec2[2] = fRec2[1];
			fRec2[1] = fRec2[0];
			fRec1[2] = fRec1[1];
			fRec1[1] = fRec1[0];
			fRec213[2] = fRec213[1];
			fRec213[1] = fRec213[0];
			fRec212[2] = fRec212[1];
			fRec212[1] = fRec212[0];
			fRec211[2] = fRec211[1];
			fRec211[1] = fRec211[0];
			fRec215[1] = fRec215[0];
		}
	}

};

} // namespace mutap_faust

#endif
