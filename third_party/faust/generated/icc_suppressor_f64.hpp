/* ------------------------------------------------------------
name: "icc_suppressor"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn icc_suppressor_f64 -es 1 -mcd 16 -mdd 1024 -mdy 33 -double -ftz 1
------------------------------------------------------------ */

#ifndef  __icc_suppressor_f64_H__
#define  __icc_suppressor_f64_H__

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
#define FAUSTCLASS icc_suppressor_f64
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

class icc_suppressor_f64SIG0 {
	
  private:
	
	int iVec1[2];
	int iRec214[2];
	int fSampleRate;
	
  public:
	
	int getNumInputsicc_suppressor_f64SIG0() {
		return 0;
	}
	int getNumOutputsicc_suppressor_f64SIG0() {
		return 1;
	}
	
	void instanceIniticc_suppressor_f64SIG0(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l215 = 0; l215 < 2; l215 = faust_wrap_add(l215, 1)) {
			iVec1[l215] = 0;
		}
		for (int l216 = 0; l216 < 2; l216 = faust_wrap_add(l216, 1)) {
			iRec214[l216] = 0;
		}
	}
	
	void fillicc_suppressor_f64SIG0(int count, double* table) {
		for (int i1 = 0; i1 < count; i1 = faust_wrap_add(i1, 1)) {
			iVec1[0] = 1;
			iRec214[0] = (faust_wrap_add(iVec1[1], iRec214[1])) % 65536;
			table[i1] = std::cos(9.587379924285257e-05 * static_cast<double>(iRec214[0]));
			iVec1[1] = iVec1[0];
			iRec214[1] = iRec214[0];
		}
	}

};

static icc_suppressor_f64SIG0* newicc_suppressor_f64SIG0() { return (icc_suppressor_f64SIG0*)new icc_suppressor_f64SIG0(); }
static void deleteicc_suppressor_f64SIG0(icc_suppressor_f64SIG0* dsp) { delete dsp; }

class icc_suppressor_f64SIG1 {
	
  private:
	
	int iVec2[2];
	int iRec216[2];
	int fSampleRate;
	
  public:
	
	int getNumInputsicc_suppressor_f64SIG1() {
		return 0;
	}
	int getNumOutputsicc_suppressor_f64SIG1() {
		return 1;
	}
	
	void instanceIniticc_suppressor_f64SIG1(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l218 = 0; l218 < 2; l218 = faust_wrap_add(l218, 1)) {
			iVec2[l218] = 0;
		}
		for (int l219 = 0; l219 < 2; l219 = faust_wrap_add(l219, 1)) {
			iRec216[l219] = 0;
		}
	}
	
	void fillicc_suppressor_f64SIG1(int count, double* table) {
		for (int i2 = 0; i2 < count; i2 = faust_wrap_add(i2, 1)) {
			iVec2[0] = 1;
			iRec216[0] = (faust_wrap_add(iVec2[1], iRec216[1])) % 65536;
			table[i2] = std::sin(9.587379924285257e-05 * static_cast<double>(iRec216[0]));
			iVec2[1] = iVec2[0];
			iRec216[1] = iRec216[0];
		}
	}

};

static icc_suppressor_f64SIG1* newicc_suppressor_f64SIG1() { return (icc_suppressor_f64SIG1*)new icc_suppressor_f64SIG1(); }
static void deleteicc_suppressor_f64SIG1(icc_suppressor_f64SIG1* dsp) { delete dsp; }

static double icc_suppressor_f64_faustpower2_f(double value) {
	return value * value;
}
static double ftbl0icc_suppressor_f64SIG0[65536];
static double ftbl1icc_suppressor_f64SIG1[65536];

class icc_suppressor_f64 : public dsp {
	
 private:
	
	int fSampleRate;
	double fConst0;
	double fConst1;
	double fConst2;
	FAUSTFLOAT fEntry0;
	int iVec0[2];
	int iRec0[3];
	FAUSTFLOAT fEntry1;
	double fConst3;
	double fConst4;
	double fConst5;
	double fConst6;
	double fConst7;
	double fConst8;
	double fConst9;
	double fConst10;
	double fConst11;
	double fConst12;
	double fConst13;
	double fConst14;
	double fConst15;
	double fRec10[3];
	double fConst16;
	double fConst17;
	double fRec9[2];
	double fConst18;
	double fConst19;
	double fConst20;
	double fConst21;
	double fConst22;
	double fConst23;
	double fConst24;
	double fRec12[3];
	double fRec11[2];
	double fConst25;
	double fConst26;
	double fConst27;
	double fConst28;
	double fConst29;
	double fConst30;
	double fConst31;
	double fRec14[3];
	double fRec13[2];
	double fConst32;
	double fConst33;
	double fConst34;
	double fConst35;
	double fConst36;
	double fConst37;
	double fConst38;
	double fRec16[3];
	double fRec15[2];
	double fConst39;
	double fConst40;
	double fConst41;
	double fConst42;
	double fConst43;
	double fConst44;
	double fConst45;
	double fRec18[3];
	double fRec17[2];
	double fConst46;
	double fConst47;
	double fConst48;
	double fConst49;
	double fConst50;
	double fConst51;
	double fConst52;
	double fRec20[3];
	double fRec19[2];
	double fConst53;
	double fConst54;
	double fConst55;
	double fConst56;
	double fConst57;
	double fConst58;
	double fConst59;
	double fRec22[3];
	double fRec21[2];
	double fConst60;
	double fConst61;
	double fConst62;
	double fConst63;
	double fConst64;
	double fConst65;
	double fConst66;
	double fRec24[3];
	double fRec23[2];
	double fConst67;
	double fConst68;
	double fConst69;
	double fConst70;
	double fConst71;
	double fConst72;
	double fConst73;
	double fRec26[3];
	double fRec25[2];
	double fConst74;
	double fConst75;
	double fConst76;
	double fConst77;
	double fConst78;
	double fConst79;
	double fConst80;
	double fRec28[3];
	double fRec27[2];
	double fConst81;
	double fConst82;
	double fConst83;
	double fConst84;
	double fConst85;
	double fConst86;
	double fConst87;
	double fRec30[3];
	double fRec29[2];
	double fConst88;
	double fConst89;
	double fConst90;
	double fConst91;
	double fConst92;
	double fConst93;
	double fConst94;
	double fRec32[3];
	double fRec31[2];
	double fConst95;
	double fConst96;
	double fConst97;
	double fConst98;
	double fConst99;
	double fConst100;
	double fConst101;
	double fRec34[3];
	double fRec33[2];
	double fConst102;
	double fConst103;
	double fConst104;
	double fConst105;
	double fConst106;
	double fConst107;
	double fConst108;
	double fRec36[3];
	double fRec35[2];
	double fConst109;
	double fConst110;
	double fConst111;
	double fConst112;
	double fConst113;
	double fConst114;
	double fConst115;
	double fRec38[3];
	double fRec37[2];
	double fConst116;
	double fConst117;
	double fConst118;
	double fConst119;
	double fConst120;
	double fConst121;
	double fConst122;
	double fRec40[3];
	double fRec39[2];
	double fConst123;
	double fConst124;
	double fConst125;
	double fConst126;
	double fConst127;
	double fConst128;
	double fConst129;
	double fRec42[3];
	double fRec41[2];
	double fConst130;
	double fConst131;
	double fConst132;
	double fConst133;
	double fConst134;
	double fConst135;
	double fConst136;
	double fRec44[3];
	double fRec43[2];
	double fConst137;
	double fConst138;
	double fConst139;
	double fConst140;
	double fConst141;
	double fConst142;
	double fConst143;
	double fRec46[3];
	double fRec45[2];
	double fConst144;
	double fConst145;
	double fConst146;
	double fConst147;
	double fConst148;
	double fConst149;
	double fConst150;
	double fRec48[3];
	double fRec47[2];
	double fConst151;
	double fConst152;
	double fConst153;
	double fConst154;
	double fConst155;
	double fConst156;
	double fConst157;
	double fRec50[3];
	double fRec49[2];
	double fConst158;
	double fConst159;
	double fConst160;
	double fConst161;
	double fConst162;
	double fConst163;
	double fConst164;
	double fRec52[3];
	double fRec51[2];
	double fConst165;
	double fConst166;
	double fConst167;
	double fConst168;
	double fConst169;
	double fConst170;
	double fConst171;
	double fRec54[3];
	double fRec53[2];
	double fConst172;
	double fConst173;
	double fConst174;
	double fConst175;
	double fConst176;
	double fConst177;
	double fConst178;
	double fRec56[3];
	double fRec55[2];
	double fConst179;
	double fConst180;
	double fConst181;
	double fConst182;
	double fConst183;
	double fConst184;
	double fConst185;
	double fRec58[3];
	double fRec57[2];
	double fConst186;
	double fConst187;
	double fConst188;
	double fConst189;
	double fConst190;
	double fConst191;
	double fConst192;
	double fRec60[3];
	double fRec59[2];
	double fConst193;
	double fConst194;
	double fConst195;
	double fConst196;
	double fConst197;
	double fConst198;
	double fConst199;
	double fRec62[3];
	double fRec61[2];
	double fConst200;
	double fConst201;
	double fConst202;
	double fConst203;
	double fConst204;
	double fConst205;
	double fConst206;
	double fRec64[3];
	double fRec63[2];
	double fConst207;
	double fConst208;
	double fConst209;
	double fConst210;
	double fConst211;
	double fConst212;
	double fConst213;
	double fRec66[3];
	double fRec65[2];
	double fConst214;
	double fConst215;
	double fConst216;
	double fConst217;
	double fConst218;
	double fConst219;
	double fConst220;
	double fRec68[3];
	double fRec67[2];
	double fConst221;
	double fConst222;
	double fConst223;
	double fConst224;
	double fConst225;
	double fConst226;
	double fConst227;
	double fRec70[3];
	double fRec69[2];
	double fConst228;
	double fConst229;
	double fConst230;
	double fConst231;
	double fConst232;
	double fConst233;
	double fConst234;
	double fRec72[3];
	double fRec71[2];
	FAUSTFLOAT fEntry2;
	FAUSTFLOAT fEntry3;
	double fConst235;
	double fRec8[2];
	double fRec7[2];
	double fRec6[2];
	double fRec5[3];
	double fConst236;
	double fConst237;
	FAUSTFLOAT fEntry4;
	double fRec73[2];
	double fRec77[3];
	double fRec76[2];
	double fRec79[3];
	double fRec78[2];
	double fRec81[3];
	double fRec80[2];
	double fRec83[3];
	double fRec82[2];
	double fRec85[3];
	double fRec84[2];
	double fRec87[3];
	double fRec86[2];
	double fRec89[3];
	double fRec88[2];
	double fRec91[3];
	double fRec90[2];
	double fRec93[3];
	double fRec92[2];
	double fRec95[3];
	double fRec94[2];
	double fRec97[3];
	double fRec96[2];
	double fRec99[3];
	double fRec98[2];
	double fRec101[3];
	double fRec100[2];
	double fRec103[3];
	double fRec102[2];
	double fRec105[3];
	double fRec104[2];
	double fRec107[3];
	double fRec106[2];
	double fRec109[3];
	double fRec108[2];
	double fRec111[3];
	double fRec110[2];
	double fRec113[3];
	double fRec112[2];
	double fRec115[3];
	double fRec114[2];
	double fRec117[3];
	double fRec116[2];
	double fRec119[3];
	double fRec118[2];
	double fRec121[3];
	double fRec120[2];
	double fRec123[3];
	double fRec122[2];
	double fRec125[3];
	double fRec124[2];
	double fRec127[3];
	double fRec126[2];
	double fRec129[3];
	double fRec128[2];
	double fRec131[3];
	double fRec130[2];
	double fRec133[3];
	double fRec132[2];
	double fRec135[3];
	double fRec134[2];
	double fRec137[3];
	double fRec136[2];
	double fRec139[3];
	double fRec138[2];
	double fRec75[2];
	double fRec74[2];
	double fRec142[2];
	double fRec141[2];
	double fRec140[3];
	double fRec147[3];
	double fRec146[2];
	double fRec149[3];
	double fRec148[2];
	double fRec151[3];
	double fRec150[2];
	double fRec153[3];
	double fRec152[2];
	double fRec155[3];
	double fRec154[2];
	double fRec157[3];
	double fRec156[2];
	double fRec159[3];
	double fRec158[2];
	double fRec161[3];
	double fRec160[2];
	double fRec163[3];
	double fRec162[2];
	double fRec165[3];
	double fRec164[2];
	double fRec167[3];
	double fRec166[2];
	double fRec169[3];
	double fRec168[2];
	double fRec171[3];
	double fRec170[2];
	double fRec173[3];
	double fRec172[2];
	double fRec175[3];
	double fRec174[2];
	double fRec177[3];
	double fRec176[2];
	double fRec179[3];
	double fRec178[2];
	double fRec181[3];
	double fRec180[2];
	double fRec183[3];
	double fRec182[2];
	double fRec185[3];
	double fRec184[2];
	double fRec187[3];
	double fRec186[2];
	double fRec189[3];
	double fRec188[2];
	double fRec191[3];
	double fRec190[2];
	double fRec193[3];
	double fRec192[2];
	double fRec195[3];
	double fRec194[2];
	double fRec197[3];
	double fRec196[2];
	double fRec199[3];
	double fRec198[2];
	double fRec201[3];
	double fRec200[2];
	double fRec203[3];
	double fRec202[2];
	double fRec205[3];
	double fRec204[2];
	double fRec207[3];
	double fRec206[2];
	double fRec209[3];
	double fRec208[2];
	double fRec145[2];
	double fRec144[2];
	double fRec143[2];
	double fRec4[3];
	double fRec210[2];
	double fRec3[3];
	double fRec2[3];
	double fRec1[3];
	double fRec213[3];
	double fRec212[3];
	double fRec211[3];
	FAUSTFLOAT fEntry5;
	double fRec215[2];
	
 public:
	icc_suppressor_f64() {
	}
	
	icc_suppressor_f64(const icc_suppressor_f64&) = default;
	
	virtual ~icc_suppressor_f64() = default;
	
	icc_suppressor_f64& operator=(const icc_suppressor_f64&) = default;
	
	void metadata(Meta* m) { 
		m->declare("analyzers.lib/amp_follower_ar:author", "Jonatan Liljedahl, revised by Romain Michon");
		m->declare("analyzers.lib/name", "Faust Analyzer Library");
		m->declare("analyzers.lib/version", "1.4.0");
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/sAndH:author", "Romain Michon");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn icc_suppressor_f64 -es 1 -mcd 16 -mdd 1024 -mdy 33 -double -ftz 1");
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
		icc_suppressor_f64SIG0* sig0 = newicc_suppressor_f64SIG0();
		sig0->instanceIniticc_suppressor_f64SIG0(sample_rate);
		sig0->fillicc_suppressor_f64SIG0(65536, ftbl0icc_suppressor_f64SIG0);
		icc_suppressor_f64SIG1* sig1 = newicc_suppressor_f64SIG1();
		sig1->instanceIniticc_suppressor_f64SIG1(sample_rate);
		sig1->fillicc_suppressor_f64SIG1(65536, ftbl1icc_suppressor_f64SIG1);
		deleteicc_suppressor_f64SIG0(sig0);
		deleteicc_suppressor_f64SIG1(sig1);
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<double>(1.92e+05, std::max<double>(1.0, static_cast<double>(fSampleRate)));
		fConst1 = 3.141592653589793 / fConst0;
		fConst2 = 0.25 * fConst0;
		fConst3 = 94.24777960769379 / fConst0;
		fConst4 = icc_suppressor_f64_faustpower2_f(1.0 - fConst3) / icc_suppressor_f64_faustpower2_f(fConst3 + 1.0);
		fConst5 = fConst4 + 1.0;
		fConst6 = 6.283185307179586 / fConst0;
		fConst7 = std::exp(-(16.00800533733655 / fConst0));
		fConst8 = 1.0 - fConst7;
		fConst9 = std::tan(18849.55592153876 / fConst0);
		fConst10 = 1.0 / fConst9;
		fConst11 = (fConst10 + 0.07142857142857142) / fConst9 + 1.0;
		fConst12 = 1.0 / (fConst9 * fConst11);
		fConst13 = 1.0 / fConst11;
		fConst14 = (fConst10 + -0.07142857142857142) / fConst9 + 1.0;
		fConst15 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst9));
		fConst16 = std::exp(-(2e+01 / fConst0));
		fConst17 = std::exp(-(2e+02 / fConst0));
		fConst18 = std::tan(16734.84786265819 / fConst0);
		fConst19 = 1.0 / fConst18;
		fConst20 = (fConst19 + 0.07142857142857142) / fConst18 + 1.0;
		fConst21 = 1.0 / (fConst18 * fConst20);
		fConst22 = 1.0 / fConst20;
		fConst23 = (fConst19 + -0.07142857142857142) / fConst18 + 1.0;
		fConst24 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst18));
		fConst25 = std::tan(14857.386251010059 / fConst0);
		fConst26 = 1.0 / fConst25;
		fConst27 = (fConst26 + 0.07142857142857142) / fConst25 + 1.0;
		fConst28 = 1.0 / (fConst25 * fConst27);
		fConst29 = 1.0 / fConst27;
		fConst30 = (fConst26 + -0.07142857142857142) / fConst25 + 1.0;
		fConst31 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst25));
		fConst32 = std::tan(13190.554704967593 / fConst0);
		fConst33 = 1.0 / fConst32;
		fConst34 = (fConst33 + 0.07142857142857142) / fConst32 + 1.0;
		fConst35 = 1.0 / (fConst32 * fConst34);
		fConst36 = 1.0 / fConst34;
		fConst37 = (fConst33 + -0.07142857142857142) / fConst32 + 1.0;
		fConst38 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst32));
		fConst39 = std::tan(11710.722901406309 / fConst0);
		fConst40 = 1.0 / fConst39;
		fConst41 = (fConst40 + 0.07142857142857142) / fConst39 + 1.0;
		fConst42 = 1.0 / (fConst39 * fConst41);
		fConst43 = 1.0 / fConst41;
		fConst44 = (fConst40 + -0.07142857142857142) / fConst39 + 1.0;
		fConst45 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst39));
		fConst46 = std::tan(10396.911573542435 / fConst0);
		fConst47 = 1.0 / fConst46;
		fConst48 = (fConst47 + 0.07142857142857142) / fConst46 + 1.0;
		fConst49 = 1.0 / (fConst46 * fConst48);
		fConst50 = 1.0 / fConst48;
		fConst51 = (fConst47 + -0.07142857142857142) / fConst46 + 1.0;
		fConst52 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst46));
		fConst53 = std::tan(9230.49509224402 / fConst0);
		fConst54 = 1.0 / fConst53;
		fConst55 = (fConst54 + 0.07142857142857142) / fConst53 + 1.0;
		fConst56 = 1.0 / (fConst53 * fConst55);
		fConst57 = 1.0 / fConst55;
		fConst58 = (fConst54 + -0.07142857142857142) / fConst53 + 1.0;
		fConst59 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst53));
		fConst60 = std::tan(8194.937414372078 / fConst0);
		fConst61 = 1.0 / fConst60;
		fConst62 = (fConst61 + 0.07142857142857142) / fConst60 + 1.0;
		fConst63 = 1.0 / (fConst60 * fConst62);
		fConst64 = 1.0 / fConst62;
		fConst65 = (fConst61 + -0.07142857142857142) / fConst60 + 1.0;
		fConst66 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst60));
		fConst67 = std::tan(7275.557654746429 / fConst0);
		fConst68 = 1.0 / fConst67;
		fConst69 = (fConst68 + 0.07142857142857142) / fConst67 + 1.0;
		fConst70 = 1.0 / (fConst67 * fConst69);
		fConst71 = 1.0 / fConst69;
		fConst72 = (fConst68 + -0.07142857142857142) / fConst67 + 1.0;
		fConst73 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst67));
		fConst74 = std::tan(6459.321958298971 / fConst0);
		fConst75 = 1.0 / fConst74;
		fConst76 = (fConst75 + 0.07142857142857142) / fConst74 + 1.0;
		fConst77 = 1.0 / (fConst74 * fConst76);
		fConst78 = 1.0 / fConst76;
		fConst79 = (fConst75 + -0.07142857142857142) / fConst74 + 1.0;
		fConst80 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst74));
		fConst81 = std::tan(5734.658721829259 / fConst0);
		fConst82 = 1.0 / fConst81;
		fConst83 = (fConst82 + 0.07142857142857142) / fConst81 + 1.0;
		fConst84 = 1.0 / (fConst81 * fConst83);
		fConst85 = 1.0 / fConst83;
		fConst86 = (fConst82 + -0.07142857142857142) / fConst81 + 1.0;
		fConst87 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst81));
		fConst88 = std::tan(5091.294545799795 / fConst0);
		fConst89 = 1.0 / fConst88;
		fConst90 = (fConst89 + 0.07142857142857142) / fConst88 + 1.0;
		fConst91 = 1.0 / (fConst88 * fConst90);
		fConst92 = 1.0 / fConst90;
		fConst93 = (fConst89 + -0.07142857142857142) / fConst88 + 1.0;
		fConst94 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst88));
		fConst95 = std::tan(4520.10859049382 / fConst0);
		fConst96 = 1.0 / fConst95;
		fConst97 = (fConst96 + 0.07142857142857142) / fConst95 + 1.0;
		fConst98 = 1.0 / (fConst95 * fConst97);
		fConst99 = 1.0 / fConst97;
		fConst100 = (fConst96 + -0.07142857142857142) / fConst95 + 1.0;
		fConst101 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst95));
		fConst102 = std::tan(4013.0032717732784 / fConst0);
		fConst103 = 1.0 / fConst102;
		fConst104 = (fConst103 + 0.07142857142857142) / fConst102 + 1.0;
		fConst105 = 1.0 / (fConst102 * fConst104);
		fConst106 = 1.0 / fConst104;
		fConst107 = (fConst103 + -0.07142857142857142) / fConst102 + 1.0;
		fConst108 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst102));
		fConst109 = std::tan(3562.789463317662 / fConst0);
		fConst110 = 1.0 / fConst109;
		fConst111 = (fConst110 + 0.07142857142857142) / fConst109 + 1.0;
		fConst112 = 1.0 / (fConst109 * fConst111);
		fConst113 = 1.0 / fConst111;
		fConst114 = (fConst110 + -0.07142857142857142) / fConst109 + 1.0;
		fConst115 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst109));
		fConst116 = std::tan(3163.0845778798307 / fConst0);
		fConst117 = 1.0 / fConst116;
		fConst118 = (fConst117 + 0.07142857142857142) / fConst116 + 1.0;
		fConst119 = 1.0 / (fConst116 * fConst118);
		fConst120 = 1.0 / fConst118;
		fConst121 = (fConst117 + -0.07142857142857142) / fConst116 + 1.0;
		fConst122 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst116));
		fConst123 = std::tan(2808.2220826780194 / fConst0);
		fConst124 = 1.0 / fConst123;
		fConst125 = (fConst124 + 0.07142857142857142) / fConst123 + 1.0;
		fConst126 = 1.0 / (fConst123 * fConst125);
		fConst127 = 1.0 / fConst125;
		fConst128 = (fConst124 + -0.07142857142857142) / fConst123 + 1.0;
		fConst129 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst123));
		fConst130 = std::tan(2493.171166142644 / fConst0);
		fConst131 = 1.0 / fConst130;
		fConst132 = (fConst131 + 0.07142857142857142) / fConst130 + 1.0;
		fConst133 = 1.0 / (fConst130 * fConst132);
		fConst134 = 1.0 / fConst132;
		fConst135 = (fConst131 + -0.07142857142857142) / fConst130 + 1.0;
		fConst136 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst130));
		fConst137 = std::tan(2213.465417150117 / fConst0);
		fConst138 = 1.0 / fConst137;
		fConst139 = (fConst138 + 0.07142857142857142) / fConst137 + 1.0;
		fConst140 = 1.0 / (fConst137 * fConst139);
		fConst141 = 1.0 / fConst139;
		fConst142 = (fConst138 + -0.07142857142857142) / fConst137 + 1.0;
		fConst143 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst137));
		fConst144 = std::tan(1965.1395056440444 / fConst0);
		fConst145 = 1.0 / fConst144;
		fConst146 = (fConst145 + 0.07142857142857142) / fConst144 + 1.0;
		fConst147 = 1.0 / (fConst144 * fConst146);
		fConst148 = 1.0 / fConst146;
		fConst149 = (fConst145 + -0.07142857142857142) / fConst144 + 1.0;
		fConst150 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst144));
		fConst151 = std::tan(1744.6729669781932 / fConst0);
		fConst152 = 1.0 / fConst151;
		fConst153 = (fConst152 + 0.07142857142857142) / fConst151 + 1.0;
		fConst154 = 1.0 / (fConst151 * fConst153);
		fConst155 = 1.0 / fConst153;
		fConst156 = (fConst152 + -0.07142857142857142) / fConst151 + 1.0;
		fConst157 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst151));
		fConst158 = std::tan(1548.9402930235765 / fConst0);
		fConst159 = 1.0 / fConst158;
		fConst160 = (fConst159 + 0.07142857142857142) / fConst158 + 1.0;
		fConst161 = 1.0 / (fConst158 * fConst160);
		fConst162 = 1.0 / fConst160;
		fConst163 = (fConst159 + -0.07142857142857142) / fConst158 + 1.0;
		fConst164 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst158));
		fConst165 = std::tan(1375.1666224916928 / fConst0);
		fConst166 = 1.0 / fConst165;
		fConst167 = (fConst166 + 0.07142857142857142) / fConst165 + 1.0;
		fConst168 = 1.0 / (fConst165 * fConst167);
		fConst169 = 1.0 / fConst167;
		fConst170 = (fConst166 + -0.07142857142857142) / fConst165 + 1.0;
		fConst171 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst165));
		fConst172 = std::tan(1220.8884023048818 / fConst0);
		fConst173 = 1.0 / fConst172;
		fConst174 = (fConst173 + 0.07142857142857142) / fConst172 + 1.0;
		fConst175 = 1.0 / (fConst172 * fConst174);
		fConst176 = 1.0 / fConst174;
		fConst177 = (fConst173 + -0.07142857142857142) / fConst172 + 1.0;
		fConst178 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst172));
		fConst179 = std::tan(1083.918462318243 / fConst0);
		fConst180 = 1.0 / fConst179;
		fConst181 = (fConst180 + 0.07142857142857142) / fConst179 + 1.0;
		fConst182 = 1.0 / (fConst179 * fConst181);
		fConst183 = 1.0 / fConst181;
		fConst184 = (fConst180 + -0.07142857142857142) / fConst179 + 1.0;
		fConst185 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst179));
		fConst186 = std::tan(962.3150082647372 / fConst0);
		fConst187 = 1.0 / fConst186;
		fConst188 = (fConst187 + 0.07142857142857142) / fConst186 + 1.0;
		fConst189 = 1.0 / (fConst186 * fConst188);
		fConst190 = 1.0 / fConst188;
		fConst191 = (fConst187 + -0.07142857142857142) / fConst186 + 1.0;
		fConst192 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst186));
		fConst193 = std::tan(854.3540933429261 / fConst0);
		fConst194 = 1.0 / fConst193;
		fConst195 = (fConst194 + 0.07142857142857142) / fConst193 + 1.0;
		fConst196 = 1.0 / (fConst193 * fConst195);
		fConst197 = 1.0 / fConst195;
		fConst198 = (fConst194 + -0.07142857142857142) / fConst193 + 1.0;
		fConst199 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst193));
		fConst200 = std::tan(758.5051781827858 / fConst0);
		fConst201 = 1.0 / fConst200;
		fConst202 = (fConst201 + 0.07142857142857142) / fConst200 + 1.0;
		fConst203 = 1.0 / (fConst200 * fConst202);
		fConst204 = 1.0 / fConst202;
		fConst205 = (fConst201 + -0.07142857142857142) / fConst200 + 1.0;
		fConst206 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst200));
		fConst207 = std::tan(673.4094327083301 / fConst0);
		fConst208 = 1.0 / fConst207;
		fConst209 = (fConst208 + 0.07142857142857142) / fConst207 + 1.0;
		fConst210 = 1.0 / (fConst207 * fConst209);
		fConst211 = 1.0 / fConst209;
		fConst212 = (fConst208 + -0.07142857142857142) / fConst207 + 1.0;
		fConst213 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst207));
		fConst214 = std::tan(597.8604722870784 / fConst0);
		fConst215 = 1.0 / fConst214;
		fConst216 = (fConst215 + 0.07142857142857142) / fConst214 + 1.0;
		fConst217 = 1.0 / (fConst214 * fConst216);
		fConst218 = 1.0 / fConst216;
		fConst219 = (fConst215 + -0.07142857142857142) / fConst214 + 1.0;
		fConst220 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst214));
		fConst221 = std::tan(530.7872550667748 / fConst0);
		fConst222 = 1.0 / fConst221;
		fConst223 = (fConst222 + 0.07142857142857142) / fConst221 + 1.0;
		fConst224 = 1.0 / (fConst221 * fConst223);
		fConst225 = 1.0 / fConst223;
		fConst226 = (fConst222 + -0.07142857142857142) / fConst221 + 1.0;
		fConst227 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst221));
		fConst228 = std::tan(471.23889803846896 / fConst0);
		fConst229 = 1.0 / fConst228;
		fConst230 = (fConst229 + 0.07142857142857142) / fConst228 + 1.0;
		fConst231 = 1.0 / (fConst228 * fConst230);
		fConst232 = 1.0 / fConst230;
		fConst233 = (fConst229 + -0.07142857142857142) / fConst228 + 1.0;
		fConst234 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fConst228));
		fConst235 = 1.0 / fConst0;
		fConst236 = std::exp(-(8.002000666915885 / fConst0));
		fConst237 = 1.0 - fConst236;
	}
	
	virtual void instanceResetUserInterface() {
		fEntry0 = static_cast<FAUSTFLOAT>(1e+02);
		fEntry1 = static_cast<FAUSTFLOAT>(0.0);
		fEntry2 = static_cast<FAUSTFLOAT>(15.0);
		fEntry3 = static_cast<FAUSTFLOAT>(0.2);
		fEntry4 = static_cast<FAUSTFLOAT>(12.0);
		fEntry5 = static_cast<FAUSTFLOAT>(2.0);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 3; l1 = faust_wrap_add(l1, 1)) {
			iRec0[l1] = 0;
		}
		for (int l2 = 0; l2 < 3; l2 = faust_wrap_add(l2, 1)) {
			fRec10[l2] = 0.0;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fRec9[l3] = 0.0;
		}
		for (int l4 = 0; l4 < 3; l4 = faust_wrap_add(l4, 1)) {
			fRec12[l4] = 0.0;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec11[l5] = 0.0;
		}
		for (int l6 = 0; l6 < 3; l6 = faust_wrap_add(l6, 1)) {
			fRec14[l6] = 0.0;
		}
		for (int l7 = 0; l7 < 2; l7 = faust_wrap_add(l7, 1)) {
			fRec13[l7] = 0.0;
		}
		for (int l8 = 0; l8 < 3; l8 = faust_wrap_add(l8, 1)) {
			fRec16[l8] = 0.0;
		}
		for (int l9 = 0; l9 < 2; l9 = faust_wrap_add(l9, 1)) {
			fRec15[l9] = 0.0;
		}
		for (int l10 = 0; l10 < 3; l10 = faust_wrap_add(l10, 1)) {
			fRec18[l10] = 0.0;
		}
		for (int l11 = 0; l11 < 2; l11 = faust_wrap_add(l11, 1)) {
			fRec17[l11] = 0.0;
		}
		for (int l12 = 0; l12 < 3; l12 = faust_wrap_add(l12, 1)) {
			fRec20[l12] = 0.0;
		}
		for (int l13 = 0; l13 < 2; l13 = faust_wrap_add(l13, 1)) {
			fRec19[l13] = 0.0;
		}
		for (int l14 = 0; l14 < 3; l14 = faust_wrap_add(l14, 1)) {
			fRec22[l14] = 0.0;
		}
		for (int l15 = 0; l15 < 2; l15 = faust_wrap_add(l15, 1)) {
			fRec21[l15] = 0.0;
		}
		for (int l16 = 0; l16 < 3; l16 = faust_wrap_add(l16, 1)) {
			fRec24[l16] = 0.0;
		}
		for (int l17 = 0; l17 < 2; l17 = faust_wrap_add(l17, 1)) {
			fRec23[l17] = 0.0;
		}
		for (int l18 = 0; l18 < 3; l18 = faust_wrap_add(l18, 1)) {
			fRec26[l18] = 0.0;
		}
		for (int l19 = 0; l19 < 2; l19 = faust_wrap_add(l19, 1)) {
			fRec25[l19] = 0.0;
		}
		for (int l20 = 0; l20 < 3; l20 = faust_wrap_add(l20, 1)) {
			fRec28[l20] = 0.0;
		}
		for (int l21 = 0; l21 < 2; l21 = faust_wrap_add(l21, 1)) {
			fRec27[l21] = 0.0;
		}
		for (int l22 = 0; l22 < 3; l22 = faust_wrap_add(l22, 1)) {
			fRec30[l22] = 0.0;
		}
		for (int l23 = 0; l23 < 2; l23 = faust_wrap_add(l23, 1)) {
			fRec29[l23] = 0.0;
		}
		for (int l24 = 0; l24 < 3; l24 = faust_wrap_add(l24, 1)) {
			fRec32[l24] = 0.0;
		}
		for (int l25 = 0; l25 < 2; l25 = faust_wrap_add(l25, 1)) {
			fRec31[l25] = 0.0;
		}
		for (int l26 = 0; l26 < 3; l26 = faust_wrap_add(l26, 1)) {
			fRec34[l26] = 0.0;
		}
		for (int l27 = 0; l27 < 2; l27 = faust_wrap_add(l27, 1)) {
			fRec33[l27] = 0.0;
		}
		for (int l28 = 0; l28 < 3; l28 = faust_wrap_add(l28, 1)) {
			fRec36[l28] = 0.0;
		}
		for (int l29 = 0; l29 < 2; l29 = faust_wrap_add(l29, 1)) {
			fRec35[l29] = 0.0;
		}
		for (int l30 = 0; l30 < 3; l30 = faust_wrap_add(l30, 1)) {
			fRec38[l30] = 0.0;
		}
		for (int l31 = 0; l31 < 2; l31 = faust_wrap_add(l31, 1)) {
			fRec37[l31] = 0.0;
		}
		for (int l32 = 0; l32 < 3; l32 = faust_wrap_add(l32, 1)) {
			fRec40[l32] = 0.0;
		}
		for (int l33 = 0; l33 < 2; l33 = faust_wrap_add(l33, 1)) {
			fRec39[l33] = 0.0;
		}
		for (int l34 = 0; l34 < 3; l34 = faust_wrap_add(l34, 1)) {
			fRec42[l34] = 0.0;
		}
		for (int l35 = 0; l35 < 2; l35 = faust_wrap_add(l35, 1)) {
			fRec41[l35] = 0.0;
		}
		for (int l36 = 0; l36 < 3; l36 = faust_wrap_add(l36, 1)) {
			fRec44[l36] = 0.0;
		}
		for (int l37 = 0; l37 < 2; l37 = faust_wrap_add(l37, 1)) {
			fRec43[l37] = 0.0;
		}
		for (int l38 = 0; l38 < 3; l38 = faust_wrap_add(l38, 1)) {
			fRec46[l38] = 0.0;
		}
		for (int l39 = 0; l39 < 2; l39 = faust_wrap_add(l39, 1)) {
			fRec45[l39] = 0.0;
		}
		for (int l40 = 0; l40 < 3; l40 = faust_wrap_add(l40, 1)) {
			fRec48[l40] = 0.0;
		}
		for (int l41 = 0; l41 < 2; l41 = faust_wrap_add(l41, 1)) {
			fRec47[l41] = 0.0;
		}
		for (int l42 = 0; l42 < 3; l42 = faust_wrap_add(l42, 1)) {
			fRec50[l42] = 0.0;
		}
		for (int l43 = 0; l43 < 2; l43 = faust_wrap_add(l43, 1)) {
			fRec49[l43] = 0.0;
		}
		for (int l44 = 0; l44 < 3; l44 = faust_wrap_add(l44, 1)) {
			fRec52[l44] = 0.0;
		}
		for (int l45 = 0; l45 < 2; l45 = faust_wrap_add(l45, 1)) {
			fRec51[l45] = 0.0;
		}
		for (int l46 = 0; l46 < 3; l46 = faust_wrap_add(l46, 1)) {
			fRec54[l46] = 0.0;
		}
		for (int l47 = 0; l47 < 2; l47 = faust_wrap_add(l47, 1)) {
			fRec53[l47] = 0.0;
		}
		for (int l48 = 0; l48 < 3; l48 = faust_wrap_add(l48, 1)) {
			fRec56[l48] = 0.0;
		}
		for (int l49 = 0; l49 < 2; l49 = faust_wrap_add(l49, 1)) {
			fRec55[l49] = 0.0;
		}
		for (int l50 = 0; l50 < 3; l50 = faust_wrap_add(l50, 1)) {
			fRec58[l50] = 0.0;
		}
		for (int l51 = 0; l51 < 2; l51 = faust_wrap_add(l51, 1)) {
			fRec57[l51] = 0.0;
		}
		for (int l52 = 0; l52 < 3; l52 = faust_wrap_add(l52, 1)) {
			fRec60[l52] = 0.0;
		}
		for (int l53 = 0; l53 < 2; l53 = faust_wrap_add(l53, 1)) {
			fRec59[l53] = 0.0;
		}
		for (int l54 = 0; l54 < 3; l54 = faust_wrap_add(l54, 1)) {
			fRec62[l54] = 0.0;
		}
		for (int l55 = 0; l55 < 2; l55 = faust_wrap_add(l55, 1)) {
			fRec61[l55] = 0.0;
		}
		for (int l56 = 0; l56 < 3; l56 = faust_wrap_add(l56, 1)) {
			fRec64[l56] = 0.0;
		}
		for (int l57 = 0; l57 < 2; l57 = faust_wrap_add(l57, 1)) {
			fRec63[l57] = 0.0;
		}
		for (int l58 = 0; l58 < 3; l58 = faust_wrap_add(l58, 1)) {
			fRec66[l58] = 0.0;
		}
		for (int l59 = 0; l59 < 2; l59 = faust_wrap_add(l59, 1)) {
			fRec65[l59] = 0.0;
		}
		for (int l60 = 0; l60 < 3; l60 = faust_wrap_add(l60, 1)) {
			fRec68[l60] = 0.0;
		}
		for (int l61 = 0; l61 < 2; l61 = faust_wrap_add(l61, 1)) {
			fRec67[l61] = 0.0;
		}
		for (int l62 = 0; l62 < 3; l62 = faust_wrap_add(l62, 1)) {
			fRec70[l62] = 0.0;
		}
		for (int l63 = 0; l63 < 2; l63 = faust_wrap_add(l63, 1)) {
			fRec69[l63] = 0.0;
		}
		for (int l64 = 0; l64 < 3; l64 = faust_wrap_add(l64, 1)) {
			fRec72[l64] = 0.0;
		}
		for (int l65 = 0; l65 < 2; l65 = faust_wrap_add(l65, 1)) {
			fRec71[l65] = 0.0;
		}
		for (int l66 = 0; l66 < 2; l66 = faust_wrap_add(l66, 1)) {
			fRec8[l66] = 0.0;
		}
		for (int l67 = 0; l67 < 2; l67 = faust_wrap_add(l67, 1)) {
			fRec7[l67] = 0.0;
		}
		for (int l68 = 0; l68 < 2; l68 = faust_wrap_add(l68, 1)) {
			fRec6[l68] = 0.0;
		}
		for (int l69 = 0; l69 < 3; l69 = faust_wrap_add(l69, 1)) {
			fRec5[l69] = 0.0;
		}
		for (int l70 = 0; l70 < 2; l70 = faust_wrap_add(l70, 1)) {
			fRec73[l70] = 0.0;
		}
		for (int l71 = 0; l71 < 3; l71 = faust_wrap_add(l71, 1)) {
			fRec77[l71] = 0.0;
		}
		for (int l72 = 0; l72 < 2; l72 = faust_wrap_add(l72, 1)) {
			fRec76[l72] = 0.0;
		}
		for (int l73 = 0; l73 < 3; l73 = faust_wrap_add(l73, 1)) {
			fRec79[l73] = 0.0;
		}
		for (int l74 = 0; l74 < 2; l74 = faust_wrap_add(l74, 1)) {
			fRec78[l74] = 0.0;
		}
		for (int l75 = 0; l75 < 3; l75 = faust_wrap_add(l75, 1)) {
			fRec81[l75] = 0.0;
		}
		for (int l76 = 0; l76 < 2; l76 = faust_wrap_add(l76, 1)) {
			fRec80[l76] = 0.0;
		}
		for (int l77 = 0; l77 < 3; l77 = faust_wrap_add(l77, 1)) {
			fRec83[l77] = 0.0;
		}
		for (int l78 = 0; l78 < 2; l78 = faust_wrap_add(l78, 1)) {
			fRec82[l78] = 0.0;
		}
		for (int l79 = 0; l79 < 3; l79 = faust_wrap_add(l79, 1)) {
			fRec85[l79] = 0.0;
		}
		for (int l80 = 0; l80 < 2; l80 = faust_wrap_add(l80, 1)) {
			fRec84[l80] = 0.0;
		}
		for (int l81 = 0; l81 < 3; l81 = faust_wrap_add(l81, 1)) {
			fRec87[l81] = 0.0;
		}
		for (int l82 = 0; l82 < 2; l82 = faust_wrap_add(l82, 1)) {
			fRec86[l82] = 0.0;
		}
		for (int l83 = 0; l83 < 3; l83 = faust_wrap_add(l83, 1)) {
			fRec89[l83] = 0.0;
		}
		for (int l84 = 0; l84 < 2; l84 = faust_wrap_add(l84, 1)) {
			fRec88[l84] = 0.0;
		}
		for (int l85 = 0; l85 < 3; l85 = faust_wrap_add(l85, 1)) {
			fRec91[l85] = 0.0;
		}
		for (int l86 = 0; l86 < 2; l86 = faust_wrap_add(l86, 1)) {
			fRec90[l86] = 0.0;
		}
		for (int l87 = 0; l87 < 3; l87 = faust_wrap_add(l87, 1)) {
			fRec93[l87] = 0.0;
		}
		for (int l88 = 0; l88 < 2; l88 = faust_wrap_add(l88, 1)) {
			fRec92[l88] = 0.0;
		}
		for (int l89 = 0; l89 < 3; l89 = faust_wrap_add(l89, 1)) {
			fRec95[l89] = 0.0;
		}
		for (int l90 = 0; l90 < 2; l90 = faust_wrap_add(l90, 1)) {
			fRec94[l90] = 0.0;
		}
		for (int l91 = 0; l91 < 3; l91 = faust_wrap_add(l91, 1)) {
			fRec97[l91] = 0.0;
		}
		for (int l92 = 0; l92 < 2; l92 = faust_wrap_add(l92, 1)) {
			fRec96[l92] = 0.0;
		}
		for (int l93 = 0; l93 < 3; l93 = faust_wrap_add(l93, 1)) {
			fRec99[l93] = 0.0;
		}
		for (int l94 = 0; l94 < 2; l94 = faust_wrap_add(l94, 1)) {
			fRec98[l94] = 0.0;
		}
		for (int l95 = 0; l95 < 3; l95 = faust_wrap_add(l95, 1)) {
			fRec101[l95] = 0.0;
		}
		for (int l96 = 0; l96 < 2; l96 = faust_wrap_add(l96, 1)) {
			fRec100[l96] = 0.0;
		}
		for (int l97 = 0; l97 < 3; l97 = faust_wrap_add(l97, 1)) {
			fRec103[l97] = 0.0;
		}
		for (int l98 = 0; l98 < 2; l98 = faust_wrap_add(l98, 1)) {
			fRec102[l98] = 0.0;
		}
		for (int l99 = 0; l99 < 3; l99 = faust_wrap_add(l99, 1)) {
			fRec105[l99] = 0.0;
		}
		for (int l100 = 0; l100 < 2; l100 = faust_wrap_add(l100, 1)) {
			fRec104[l100] = 0.0;
		}
		for (int l101 = 0; l101 < 3; l101 = faust_wrap_add(l101, 1)) {
			fRec107[l101] = 0.0;
		}
		for (int l102 = 0; l102 < 2; l102 = faust_wrap_add(l102, 1)) {
			fRec106[l102] = 0.0;
		}
		for (int l103 = 0; l103 < 3; l103 = faust_wrap_add(l103, 1)) {
			fRec109[l103] = 0.0;
		}
		for (int l104 = 0; l104 < 2; l104 = faust_wrap_add(l104, 1)) {
			fRec108[l104] = 0.0;
		}
		for (int l105 = 0; l105 < 3; l105 = faust_wrap_add(l105, 1)) {
			fRec111[l105] = 0.0;
		}
		for (int l106 = 0; l106 < 2; l106 = faust_wrap_add(l106, 1)) {
			fRec110[l106] = 0.0;
		}
		for (int l107 = 0; l107 < 3; l107 = faust_wrap_add(l107, 1)) {
			fRec113[l107] = 0.0;
		}
		for (int l108 = 0; l108 < 2; l108 = faust_wrap_add(l108, 1)) {
			fRec112[l108] = 0.0;
		}
		for (int l109 = 0; l109 < 3; l109 = faust_wrap_add(l109, 1)) {
			fRec115[l109] = 0.0;
		}
		for (int l110 = 0; l110 < 2; l110 = faust_wrap_add(l110, 1)) {
			fRec114[l110] = 0.0;
		}
		for (int l111 = 0; l111 < 3; l111 = faust_wrap_add(l111, 1)) {
			fRec117[l111] = 0.0;
		}
		for (int l112 = 0; l112 < 2; l112 = faust_wrap_add(l112, 1)) {
			fRec116[l112] = 0.0;
		}
		for (int l113 = 0; l113 < 3; l113 = faust_wrap_add(l113, 1)) {
			fRec119[l113] = 0.0;
		}
		for (int l114 = 0; l114 < 2; l114 = faust_wrap_add(l114, 1)) {
			fRec118[l114] = 0.0;
		}
		for (int l115 = 0; l115 < 3; l115 = faust_wrap_add(l115, 1)) {
			fRec121[l115] = 0.0;
		}
		for (int l116 = 0; l116 < 2; l116 = faust_wrap_add(l116, 1)) {
			fRec120[l116] = 0.0;
		}
		for (int l117 = 0; l117 < 3; l117 = faust_wrap_add(l117, 1)) {
			fRec123[l117] = 0.0;
		}
		for (int l118 = 0; l118 < 2; l118 = faust_wrap_add(l118, 1)) {
			fRec122[l118] = 0.0;
		}
		for (int l119 = 0; l119 < 3; l119 = faust_wrap_add(l119, 1)) {
			fRec125[l119] = 0.0;
		}
		for (int l120 = 0; l120 < 2; l120 = faust_wrap_add(l120, 1)) {
			fRec124[l120] = 0.0;
		}
		for (int l121 = 0; l121 < 3; l121 = faust_wrap_add(l121, 1)) {
			fRec127[l121] = 0.0;
		}
		for (int l122 = 0; l122 < 2; l122 = faust_wrap_add(l122, 1)) {
			fRec126[l122] = 0.0;
		}
		for (int l123 = 0; l123 < 3; l123 = faust_wrap_add(l123, 1)) {
			fRec129[l123] = 0.0;
		}
		for (int l124 = 0; l124 < 2; l124 = faust_wrap_add(l124, 1)) {
			fRec128[l124] = 0.0;
		}
		for (int l125 = 0; l125 < 3; l125 = faust_wrap_add(l125, 1)) {
			fRec131[l125] = 0.0;
		}
		for (int l126 = 0; l126 < 2; l126 = faust_wrap_add(l126, 1)) {
			fRec130[l126] = 0.0;
		}
		for (int l127 = 0; l127 < 3; l127 = faust_wrap_add(l127, 1)) {
			fRec133[l127] = 0.0;
		}
		for (int l128 = 0; l128 < 2; l128 = faust_wrap_add(l128, 1)) {
			fRec132[l128] = 0.0;
		}
		for (int l129 = 0; l129 < 3; l129 = faust_wrap_add(l129, 1)) {
			fRec135[l129] = 0.0;
		}
		for (int l130 = 0; l130 < 2; l130 = faust_wrap_add(l130, 1)) {
			fRec134[l130] = 0.0;
		}
		for (int l131 = 0; l131 < 3; l131 = faust_wrap_add(l131, 1)) {
			fRec137[l131] = 0.0;
		}
		for (int l132 = 0; l132 < 2; l132 = faust_wrap_add(l132, 1)) {
			fRec136[l132] = 0.0;
		}
		for (int l133 = 0; l133 < 3; l133 = faust_wrap_add(l133, 1)) {
			fRec139[l133] = 0.0;
		}
		for (int l134 = 0; l134 < 2; l134 = faust_wrap_add(l134, 1)) {
			fRec138[l134] = 0.0;
		}
		for (int l135 = 0; l135 < 2; l135 = faust_wrap_add(l135, 1)) {
			fRec75[l135] = 0.0;
		}
		for (int l136 = 0; l136 < 2; l136 = faust_wrap_add(l136, 1)) {
			fRec74[l136] = 0.0;
		}
		for (int l137 = 0; l137 < 2; l137 = faust_wrap_add(l137, 1)) {
			fRec142[l137] = 0.0;
		}
		for (int l138 = 0; l138 < 2; l138 = faust_wrap_add(l138, 1)) {
			fRec141[l138] = 0.0;
		}
		for (int l139 = 0; l139 < 3; l139 = faust_wrap_add(l139, 1)) {
			fRec140[l139] = 0.0;
		}
		for (int l140 = 0; l140 < 3; l140 = faust_wrap_add(l140, 1)) {
			fRec147[l140] = 0.0;
		}
		for (int l141 = 0; l141 < 2; l141 = faust_wrap_add(l141, 1)) {
			fRec146[l141] = 0.0;
		}
		for (int l142 = 0; l142 < 3; l142 = faust_wrap_add(l142, 1)) {
			fRec149[l142] = 0.0;
		}
		for (int l143 = 0; l143 < 2; l143 = faust_wrap_add(l143, 1)) {
			fRec148[l143] = 0.0;
		}
		for (int l144 = 0; l144 < 3; l144 = faust_wrap_add(l144, 1)) {
			fRec151[l144] = 0.0;
		}
		for (int l145 = 0; l145 < 2; l145 = faust_wrap_add(l145, 1)) {
			fRec150[l145] = 0.0;
		}
		for (int l146 = 0; l146 < 3; l146 = faust_wrap_add(l146, 1)) {
			fRec153[l146] = 0.0;
		}
		for (int l147 = 0; l147 < 2; l147 = faust_wrap_add(l147, 1)) {
			fRec152[l147] = 0.0;
		}
		for (int l148 = 0; l148 < 3; l148 = faust_wrap_add(l148, 1)) {
			fRec155[l148] = 0.0;
		}
		for (int l149 = 0; l149 < 2; l149 = faust_wrap_add(l149, 1)) {
			fRec154[l149] = 0.0;
		}
		for (int l150 = 0; l150 < 3; l150 = faust_wrap_add(l150, 1)) {
			fRec157[l150] = 0.0;
		}
		for (int l151 = 0; l151 < 2; l151 = faust_wrap_add(l151, 1)) {
			fRec156[l151] = 0.0;
		}
		for (int l152 = 0; l152 < 3; l152 = faust_wrap_add(l152, 1)) {
			fRec159[l152] = 0.0;
		}
		for (int l153 = 0; l153 < 2; l153 = faust_wrap_add(l153, 1)) {
			fRec158[l153] = 0.0;
		}
		for (int l154 = 0; l154 < 3; l154 = faust_wrap_add(l154, 1)) {
			fRec161[l154] = 0.0;
		}
		for (int l155 = 0; l155 < 2; l155 = faust_wrap_add(l155, 1)) {
			fRec160[l155] = 0.0;
		}
		for (int l156 = 0; l156 < 3; l156 = faust_wrap_add(l156, 1)) {
			fRec163[l156] = 0.0;
		}
		for (int l157 = 0; l157 < 2; l157 = faust_wrap_add(l157, 1)) {
			fRec162[l157] = 0.0;
		}
		for (int l158 = 0; l158 < 3; l158 = faust_wrap_add(l158, 1)) {
			fRec165[l158] = 0.0;
		}
		for (int l159 = 0; l159 < 2; l159 = faust_wrap_add(l159, 1)) {
			fRec164[l159] = 0.0;
		}
		for (int l160 = 0; l160 < 3; l160 = faust_wrap_add(l160, 1)) {
			fRec167[l160] = 0.0;
		}
		for (int l161 = 0; l161 < 2; l161 = faust_wrap_add(l161, 1)) {
			fRec166[l161] = 0.0;
		}
		for (int l162 = 0; l162 < 3; l162 = faust_wrap_add(l162, 1)) {
			fRec169[l162] = 0.0;
		}
		for (int l163 = 0; l163 < 2; l163 = faust_wrap_add(l163, 1)) {
			fRec168[l163] = 0.0;
		}
		for (int l164 = 0; l164 < 3; l164 = faust_wrap_add(l164, 1)) {
			fRec171[l164] = 0.0;
		}
		for (int l165 = 0; l165 < 2; l165 = faust_wrap_add(l165, 1)) {
			fRec170[l165] = 0.0;
		}
		for (int l166 = 0; l166 < 3; l166 = faust_wrap_add(l166, 1)) {
			fRec173[l166] = 0.0;
		}
		for (int l167 = 0; l167 < 2; l167 = faust_wrap_add(l167, 1)) {
			fRec172[l167] = 0.0;
		}
		for (int l168 = 0; l168 < 3; l168 = faust_wrap_add(l168, 1)) {
			fRec175[l168] = 0.0;
		}
		for (int l169 = 0; l169 < 2; l169 = faust_wrap_add(l169, 1)) {
			fRec174[l169] = 0.0;
		}
		for (int l170 = 0; l170 < 3; l170 = faust_wrap_add(l170, 1)) {
			fRec177[l170] = 0.0;
		}
		for (int l171 = 0; l171 < 2; l171 = faust_wrap_add(l171, 1)) {
			fRec176[l171] = 0.0;
		}
		for (int l172 = 0; l172 < 3; l172 = faust_wrap_add(l172, 1)) {
			fRec179[l172] = 0.0;
		}
		for (int l173 = 0; l173 < 2; l173 = faust_wrap_add(l173, 1)) {
			fRec178[l173] = 0.0;
		}
		for (int l174 = 0; l174 < 3; l174 = faust_wrap_add(l174, 1)) {
			fRec181[l174] = 0.0;
		}
		for (int l175 = 0; l175 < 2; l175 = faust_wrap_add(l175, 1)) {
			fRec180[l175] = 0.0;
		}
		for (int l176 = 0; l176 < 3; l176 = faust_wrap_add(l176, 1)) {
			fRec183[l176] = 0.0;
		}
		for (int l177 = 0; l177 < 2; l177 = faust_wrap_add(l177, 1)) {
			fRec182[l177] = 0.0;
		}
		for (int l178 = 0; l178 < 3; l178 = faust_wrap_add(l178, 1)) {
			fRec185[l178] = 0.0;
		}
		for (int l179 = 0; l179 < 2; l179 = faust_wrap_add(l179, 1)) {
			fRec184[l179] = 0.0;
		}
		for (int l180 = 0; l180 < 3; l180 = faust_wrap_add(l180, 1)) {
			fRec187[l180] = 0.0;
		}
		for (int l181 = 0; l181 < 2; l181 = faust_wrap_add(l181, 1)) {
			fRec186[l181] = 0.0;
		}
		for (int l182 = 0; l182 < 3; l182 = faust_wrap_add(l182, 1)) {
			fRec189[l182] = 0.0;
		}
		for (int l183 = 0; l183 < 2; l183 = faust_wrap_add(l183, 1)) {
			fRec188[l183] = 0.0;
		}
		for (int l184 = 0; l184 < 3; l184 = faust_wrap_add(l184, 1)) {
			fRec191[l184] = 0.0;
		}
		for (int l185 = 0; l185 < 2; l185 = faust_wrap_add(l185, 1)) {
			fRec190[l185] = 0.0;
		}
		for (int l186 = 0; l186 < 3; l186 = faust_wrap_add(l186, 1)) {
			fRec193[l186] = 0.0;
		}
		for (int l187 = 0; l187 < 2; l187 = faust_wrap_add(l187, 1)) {
			fRec192[l187] = 0.0;
		}
		for (int l188 = 0; l188 < 3; l188 = faust_wrap_add(l188, 1)) {
			fRec195[l188] = 0.0;
		}
		for (int l189 = 0; l189 < 2; l189 = faust_wrap_add(l189, 1)) {
			fRec194[l189] = 0.0;
		}
		for (int l190 = 0; l190 < 3; l190 = faust_wrap_add(l190, 1)) {
			fRec197[l190] = 0.0;
		}
		for (int l191 = 0; l191 < 2; l191 = faust_wrap_add(l191, 1)) {
			fRec196[l191] = 0.0;
		}
		for (int l192 = 0; l192 < 3; l192 = faust_wrap_add(l192, 1)) {
			fRec199[l192] = 0.0;
		}
		for (int l193 = 0; l193 < 2; l193 = faust_wrap_add(l193, 1)) {
			fRec198[l193] = 0.0;
		}
		for (int l194 = 0; l194 < 3; l194 = faust_wrap_add(l194, 1)) {
			fRec201[l194] = 0.0;
		}
		for (int l195 = 0; l195 < 2; l195 = faust_wrap_add(l195, 1)) {
			fRec200[l195] = 0.0;
		}
		for (int l196 = 0; l196 < 3; l196 = faust_wrap_add(l196, 1)) {
			fRec203[l196] = 0.0;
		}
		for (int l197 = 0; l197 < 2; l197 = faust_wrap_add(l197, 1)) {
			fRec202[l197] = 0.0;
		}
		for (int l198 = 0; l198 < 3; l198 = faust_wrap_add(l198, 1)) {
			fRec205[l198] = 0.0;
		}
		for (int l199 = 0; l199 < 2; l199 = faust_wrap_add(l199, 1)) {
			fRec204[l199] = 0.0;
		}
		for (int l200 = 0; l200 < 3; l200 = faust_wrap_add(l200, 1)) {
			fRec207[l200] = 0.0;
		}
		for (int l201 = 0; l201 < 2; l201 = faust_wrap_add(l201, 1)) {
			fRec206[l201] = 0.0;
		}
		for (int l202 = 0; l202 < 3; l202 = faust_wrap_add(l202, 1)) {
			fRec209[l202] = 0.0;
		}
		for (int l203 = 0; l203 < 2; l203 = faust_wrap_add(l203, 1)) {
			fRec208[l203] = 0.0;
		}
		for (int l204 = 0; l204 < 2; l204 = faust_wrap_add(l204, 1)) {
			fRec145[l204] = 0.0;
		}
		for (int l205 = 0; l205 < 2; l205 = faust_wrap_add(l205, 1)) {
			fRec144[l205] = 0.0;
		}
		for (int l206 = 0; l206 < 2; l206 = faust_wrap_add(l206, 1)) {
			fRec143[l206] = 0.0;
		}
		for (int l207 = 0; l207 < 3; l207 = faust_wrap_add(l207, 1)) {
			fRec4[l207] = 0.0;
		}
		for (int l208 = 0; l208 < 2; l208 = faust_wrap_add(l208, 1)) {
			fRec210[l208] = 0.0;
		}
		for (int l209 = 0; l209 < 3; l209 = faust_wrap_add(l209, 1)) {
			fRec3[l209] = 0.0;
		}
		for (int l210 = 0; l210 < 3; l210 = faust_wrap_add(l210, 1)) {
			fRec2[l210] = 0.0;
		}
		for (int l211 = 0; l211 < 3; l211 = faust_wrap_add(l211, 1)) {
			fRec1[l211] = 0.0;
		}
		for (int l212 = 0; l212 < 3; l212 = faust_wrap_add(l212, 1)) {
			fRec213[l212] = 0.0;
		}
		for (int l213 = 0; l213 < 3; l213 = faust_wrap_add(l213, 1)) {
			fRec212[l213] = 0.0;
		}
		for (int l214 = 0; l214 < 3; l214 = faust_wrap_add(l214, 1)) {
			fRec211[l214] = 0.0;
		}
		for (int l217 = 0; l217 < 2; l217 = faust_wrap_add(l217, 1)) {
			fRec215[l217] = 0.0;
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
	
	virtual icc_suppressor_f64* clone() {
		return new icc_suppressor_f64(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("icc_suppressor");
		ui_interface->addNumEntry("guard", &fEntry0, FAUSTFLOAT(1e+02), FAUSTFLOAT(1.0), FAUSTFLOAT(8e+03), FAUSTFLOAT(1.0));
		ui_interface->addNumEntry("hold_time", &fEntry3, FAUSTFLOAT(0.2), FAUSTFLOAT(0.001), FAUSTFLOAT(1e+01), FAUSTFLOAT(0.001));
		ui_interface->addNumEntry("max_depth", &fEntry4, FAUSTFLOAT(12.0), FAUSTFLOAT(0.0), FAUSTFLOAT(6e+01), FAUSTFLOAT(0.1));
		ui_interface->addNumEntry("notch_bypass", &fEntry1, FAUSTFLOAT(0.0), FAUSTFLOAT(0.0), FAUSTFLOAT(1.0), FAUSTFLOAT(1.0));
		ui_interface->addNumEntry("prom_thresh", &fEntry2, FAUSTFLOAT(15.0), FAUSTFLOAT(0.0), FAUSTFLOAT(6e+01), FAUSTFLOAT(0.1));
		ui_interface->addNumEntry("shift", &fEntry5, FAUSTFLOAT(2.0), FAUSTFLOAT(-2e+01), FAUSTFLOAT(2e+01), FAUSTFLOAT(0.01));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* output0 = outputs[0];
		double fSlow0 = std::tan(fConst1 * (fConst2 - static_cast<double>(fEntry0)));
		double fSlow1 = 1.0 / fSlow0;
		double fSlow2 = (fSlow1 + 0.5176380902050413) / fSlow0 + 1.0;
		double fSlow3 = 2.0 / fSlow2;
		double fSlow4 = 1.0 / ((fSlow1 + 1.414213562373095) / fSlow0 + 1.0);
		double fSlow5 = 1.0 / ((fSlow1 + 1.9318516525781364) / fSlow0 + 1.0);
		int iSlow6 = static_cast<int>(static_cast<double>(fEntry1));
		double fSlow7 = static_cast<double>(fEntry2);
		double fSlow8 = static_cast<double>(fEntry3);
		double fSlow9 = 4.0 * fSlow8;
		int iSlow10 = std::fabs(fSlow9) < 2.220446049250313e-16;
		double fSlow11 = ((iSlow10) ? 0.0 : std::exp(-(fConst235 / ((iSlow10) ? 1.0 : fSlow9))));
		int iSlow12 = std::fabs(fSlow8) < 2.220446049250313e-16;
		double fSlow13 = ((iSlow12) ? 0.0 : std::exp(-(fConst235 / ((iSlow12) ? 1.0 : fSlow8))));
		double fSlow14 = fConst237 * (1.0 - std::pow(1e+01, -(0.05 * static_cast<double>(fEntry4))));
		double fSlow15 = (fSlow1 + -1.9318516525781364) / fSlow0 + 1.0;
		double fSlow16 = 2.0 * (1.0 - 1.0 / icc_suppressor_f64_faustpower2_f(fSlow0));
		double fSlow17 = (fSlow1 + -1.414213562373095) / fSlow0 + 1.0;
		double fSlow18 = 1.0 / fSlow2;
		double fSlow19 = (fSlow1 + -0.5176380902050413) / fSlow0 + 1.0;
		double fSlow20 = fConst235 * static_cast<double>(fEntry5);
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			iVec0[0] = 1;
			int iTemp0 = faust_wrap_sub(1, iVec0[1]);
			iRec0[0] = faust_wrap_sub(iTemp0, iRec0[2]);
			double fTemp1 = static_cast<double>(iRec0[0]);
			double fTemp2 = static_cast<double>(input0[i0]);
			double fTemp3 = fTemp2 - fConst13 * (fConst14 * fRec10[2] + fConst15 * fRec10[1]);
			fRec10[0] = ((std::fabs(fTemp3) > 2.2250738585072014e-308) ? fTemp3 : 0.0);
			double fTemp4 = std::fabs(fConst12 * (fRec10[0] - fRec10[2]));
			double fTemp5 = ((fTemp4 > fRec9[1]) ? fConst17 : fConst16);
			double fTemp6 = fTemp4 * (1.0 - fTemp5) + fRec9[1] * fTemp5;
			fRec9[0] = ((std::fabs(fTemp6) > 2.2250738585072014e-308) ? fTemp6 : 0.0);
			double fTemp7 = fTemp2 - fConst22 * (fConst23 * fRec12[2] + fConst24 * fRec12[1]);
			fRec12[0] = ((std::fabs(fTemp7) > 2.2250738585072014e-308) ? fTemp7 : 0.0);
			double fTemp8 = std::fabs(fConst21 * (fRec12[0] - fRec12[2]));
			double fTemp9 = ((fTemp8 > fRec11[1]) ? fConst17 : fConst16);
			double fTemp10 = fTemp8 * (1.0 - fTemp9) + fRec11[1] * fTemp9;
			fRec11[0] = ((std::fabs(fTemp10) > 2.2250738585072014e-308) ? fTemp10 : 0.0);
			double fTemp11 = fTemp2 - fConst29 * (fConst30 * fRec14[2] + fConst31 * fRec14[1]);
			fRec14[0] = ((std::fabs(fTemp11) > 2.2250738585072014e-308) ? fTemp11 : 0.0);
			double fTemp12 = std::fabs(fConst28 * (fRec14[0] - fRec14[2]));
			double fTemp13 = ((fTemp12 > fRec13[1]) ? fConst17 : fConst16);
			double fTemp14 = fTemp12 * (1.0 - fTemp13) + fRec13[1] * fTemp13;
			fRec13[0] = ((std::fabs(fTemp14) > 2.2250738585072014e-308) ? fTemp14 : 0.0);
			double fTemp15 = fTemp2 - fConst36 * (fConst37 * fRec16[2] + fConst38 * fRec16[1]);
			fRec16[0] = ((std::fabs(fTemp15) > 2.2250738585072014e-308) ? fTemp15 : 0.0);
			double fTemp16 = std::fabs(fConst35 * (fRec16[0] - fRec16[2]));
			double fTemp17 = ((fTemp16 > fRec15[1]) ? fConst17 : fConst16);
			double fTemp18 = fTemp16 * (1.0 - fTemp17) + fRec15[1] * fTemp17;
			fRec15[0] = ((std::fabs(fTemp18) > 2.2250738585072014e-308) ? fTemp18 : 0.0);
			double fTemp19 = fTemp2 - fConst43 * (fConst44 * fRec18[2] + fConst45 * fRec18[1]);
			fRec18[0] = ((std::fabs(fTemp19) > 2.2250738585072014e-308) ? fTemp19 : 0.0);
			double fTemp20 = std::fabs(fConst42 * (fRec18[0] - fRec18[2]));
			double fTemp21 = ((fTemp20 > fRec17[1]) ? fConst17 : fConst16);
			double fTemp22 = fTemp20 * (1.0 - fTemp21) + fRec17[1] * fTemp21;
			fRec17[0] = ((std::fabs(fTemp22) > 2.2250738585072014e-308) ? fTemp22 : 0.0);
			double fTemp23 = fTemp2 - fConst50 * (fConst51 * fRec20[2] + fConst52 * fRec20[1]);
			fRec20[0] = ((std::fabs(fTemp23) > 2.2250738585072014e-308) ? fTemp23 : 0.0);
			double fTemp24 = std::fabs(fConst49 * (fRec20[0] - fRec20[2]));
			double fTemp25 = ((fTemp24 > fRec19[1]) ? fConst17 : fConst16);
			double fTemp26 = fTemp24 * (1.0 - fTemp25) + fRec19[1] * fTemp25;
			fRec19[0] = ((std::fabs(fTemp26) > 2.2250738585072014e-308) ? fTemp26 : 0.0);
			double fTemp27 = fTemp2 - fConst57 * (fConst58 * fRec22[2] + fConst59 * fRec22[1]);
			fRec22[0] = ((std::fabs(fTemp27) > 2.2250738585072014e-308) ? fTemp27 : 0.0);
			double fTemp28 = std::fabs(fConst56 * (fRec22[0] - fRec22[2]));
			double fTemp29 = ((fTemp28 > fRec21[1]) ? fConst17 : fConst16);
			double fTemp30 = fTemp28 * (1.0 - fTemp29) + fRec21[1] * fTemp29;
			fRec21[0] = ((std::fabs(fTemp30) > 2.2250738585072014e-308) ? fTemp30 : 0.0);
			double fTemp31 = fTemp2 - fConst64 * (fConst65 * fRec24[2] + fConst66 * fRec24[1]);
			fRec24[0] = ((std::fabs(fTemp31) > 2.2250738585072014e-308) ? fTemp31 : 0.0);
			double fTemp32 = std::fabs(fConst63 * (fRec24[0] - fRec24[2]));
			double fTemp33 = ((fTemp32 > fRec23[1]) ? fConst17 : fConst16);
			double fTemp34 = fTemp32 * (1.0 - fTemp33) + fRec23[1] * fTemp33;
			fRec23[0] = ((std::fabs(fTemp34) > 2.2250738585072014e-308) ? fTemp34 : 0.0);
			double fTemp35 = fTemp2 - fConst71 * (fConst72 * fRec26[2] + fConst73 * fRec26[1]);
			fRec26[0] = ((std::fabs(fTemp35) > 2.2250738585072014e-308) ? fTemp35 : 0.0);
			double fTemp36 = std::fabs(fConst70 * (fRec26[0] - fRec26[2]));
			double fTemp37 = ((fTemp36 > fRec25[1]) ? fConst17 : fConst16);
			double fTemp38 = fTemp36 * (1.0 - fTemp37) + fRec25[1] * fTemp37;
			fRec25[0] = ((std::fabs(fTemp38) > 2.2250738585072014e-308) ? fTemp38 : 0.0);
			double fTemp39 = fTemp2 - fConst78 * (fConst79 * fRec28[2] + fConst80 * fRec28[1]);
			fRec28[0] = ((std::fabs(fTemp39) > 2.2250738585072014e-308) ? fTemp39 : 0.0);
			double fTemp40 = std::fabs(fConst77 * (fRec28[0] - fRec28[2]));
			double fTemp41 = ((fTemp40 > fRec27[1]) ? fConst17 : fConst16);
			double fTemp42 = fTemp40 * (1.0 - fTemp41) + fRec27[1] * fTemp41;
			fRec27[0] = ((std::fabs(fTemp42) > 2.2250738585072014e-308) ? fTemp42 : 0.0);
			double fTemp43 = fTemp2 - fConst85 * (fConst86 * fRec30[2] + fConst87 * fRec30[1]);
			fRec30[0] = ((std::fabs(fTemp43) > 2.2250738585072014e-308) ? fTemp43 : 0.0);
			double fTemp44 = std::fabs(fConst84 * (fRec30[0] - fRec30[2]));
			double fTemp45 = ((fTemp44 > fRec29[1]) ? fConst17 : fConst16);
			double fTemp46 = fTemp44 * (1.0 - fTemp45) + fRec29[1] * fTemp45;
			fRec29[0] = ((std::fabs(fTemp46) > 2.2250738585072014e-308) ? fTemp46 : 0.0);
			double fTemp47 = fTemp2 - fConst92 * (fConst93 * fRec32[2] + fConst94 * fRec32[1]);
			fRec32[0] = ((std::fabs(fTemp47) > 2.2250738585072014e-308) ? fTemp47 : 0.0);
			double fTemp48 = std::fabs(fConst91 * (fRec32[0] - fRec32[2]));
			double fTemp49 = ((fTemp48 > fRec31[1]) ? fConst17 : fConst16);
			double fTemp50 = fTemp48 * (1.0 - fTemp49) + fRec31[1] * fTemp49;
			fRec31[0] = ((std::fabs(fTemp50) > 2.2250738585072014e-308) ? fTemp50 : 0.0);
			double fTemp51 = fTemp2 - fConst99 * (fConst100 * fRec34[2] + fConst101 * fRec34[1]);
			fRec34[0] = ((std::fabs(fTemp51) > 2.2250738585072014e-308) ? fTemp51 : 0.0);
			double fTemp52 = std::fabs(fConst98 * (fRec34[0] - fRec34[2]));
			double fTemp53 = ((fTemp52 > fRec33[1]) ? fConst17 : fConst16);
			double fTemp54 = fTemp52 * (1.0 - fTemp53) + fRec33[1] * fTemp53;
			fRec33[0] = ((std::fabs(fTemp54) > 2.2250738585072014e-308) ? fTemp54 : 0.0);
			double fTemp55 = fTemp2 - fConst106 * (fConst107 * fRec36[2] + fConst108 * fRec36[1]);
			fRec36[0] = ((std::fabs(fTemp55) > 2.2250738585072014e-308) ? fTemp55 : 0.0);
			double fTemp56 = std::fabs(fConst105 * (fRec36[0] - fRec36[2]));
			double fTemp57 = ((fTemp56 > fRec35[1]) ? fConst17 : fConst16);
			double fTemp58 = fTemp56 * (1.0 - fTemp57) + fRec35[1] * fTemp57;
			fRec35[0] = ((std::fabs(fTemp58) > 2.2250738585072014e-308) ? fTemp58 : 0.0);
			double fTemp59 = fTemp2 - fConst113 * (fConst114 * fRec38[2] + fConst115 * fRec38[1]);
			fRec38[0] = ((std::fabs(fTemp59) > 2.2250738585072014e-308) ? fTemp59 : 0.0);
			double fTemp60 = std::fabs(fConst112 * (fRec38[0] - fRec38[2]));
			double fTemp61 = ((fTemp60 > fRec37[1]) ? fConst17 : fConst16);
			double fTemp62 = fTemp60 * (1.0 - fTemp61) + fRec37[1] * fTemp61;
			fRec37[0] = ((std::fabs(fTemp62) > 2.2250738585072014e-308) ? fTemp62 : 0.0);
			double fTemp63 = fTemp2 - fConst120 * (fConst121 * fRec40[2] + fConst122 * fRec40[1]);
			fRec40[0] = ((std::fabs(fTemp63) > 2.2250738585072014e-308) ? fTemp63 : 0.0);
			double fTemp64 = std::fabs(fConst119 * (fRec40[0] - fRec40[2]));
			double fTemp65 = ((fTemp64 > fRec39[1]) ? fConst17 : fConst16);
			double fTemp66 = fTemp64 * (1.0 - fTemp65) + fRec39[1] * fTemp65;
			fRec39[0] = ((std::fabs(fTemp66) > 2.2250738585072014e-308) ? fTemp66 : 0.0);
			double fTemp67 = fTemp2 - fConst127 * (fConst128 * fRec42[2] + fConst129 * fRec42[1]);
			fRec42[0] = ((std::fabs(fTemp67) > 2.2250738585072014e-308) ? fTemp67 : 0.0);
			double fTemp68 = std::fabs(fConst126 * (fRec42[0] - fRec42[2]));
			double fTemp69 = ((fTemp68 > fRec41[1]) ? fConst17 : fConst16);
			double fTemp70 = fTemp68 * (1.0 - fTemp69) + fRec41[1] * fTemp69;
			fRec41[0] = ((std::fabs(fTemp70) > 2.2250738585072014e-308) ? fTemp70 : 0.0);
			double fTemp71 = fTemp2 - fConst134 * (fConst135 * fRec44[2] + fConst136 * fRec44[1]);
			fRec44[0] = ((std::fabs(fTemp71) > 2.2250738585072014e-308) ? fTemp71 : 0.0);
			double fTemp72 = std::fabs(fConst133 * (fRec44[0] - fRec44[2]));
			double fTemp73 = ((fTemp72 > fRec43[1]) ? fConst17 : fConst16);
			double fTemp74 = fTemp72 * (1.0 - fTemp73) + fRec43[1] * fTemp73;
			fRec43[0] = ((std::fabs(fTemp74) > 2.2250738585072014e-308) ? fTemp74 : 0.0);
			double fTemp75 = fTemp2 - fConst141 * (fConst142 * fRec46[2] + fConst143 * fRec46[1]);
			fRec46[0] = ((std::fabs(fTemp75) > 2.2250738585072014e-308) ? fTemp75 : 0.0);
			double fTemp76 = std::fabs(fConst140 * (fRec46[0] - fRec46[2]));
			double fTemp77 = ((fTemp76 > fRec45[1]) ? fConst17 : fConst16);
			double fTemp78 = fTemp76 * (1.0 - fTemp77) + fRec45[1] * fTemp77;
			fRec45[0] = ((std::fabs(fTemp78) > 2.2250738585072014e-308) ? fTemp78 : 0.0);
			double fTemp79 = fTemp2 - fConst148 * (fConst149 * fRec48[2] + fConst150 * fRec48[1]);
			fRec48[0] = ((std::fabs(fTemp79) > 2.2250738585072014e-308) ? fTemp79 : 0.0);
			double fTemp80 = std::fabs(fConst147 * (fRec48[0] - fRec48[2]));
			double fTemp81 = ((fTemp80 > fRec47[1]) ? fConst17 : fConst16);
			double fTemp82 = fTemp80 * (1.0 - fTemp81) + fRec47[1] * fTemp81;
			fRec47[0] = ((std::fabs(fTemp82) > 2.2250738585072014e-308) ? fTemp82 : 0.0);
			double fTemp83 = fTemp2 - fConst155 * (fConst156 * fRec50[2] + fConst157 * fRec50[1]);
			fRec50[0] = ((std::fabs(fTemp83) > 2.2250738585072014e-308) ? fTemp83 : 0.0);
			double fTemp84 = std::fabs(fConst154 * (fRec50[0] - fRec50[2]));
			double fTemp85 = ((fTemp84 > fRec49[1]) ? fConst17 : fConst16);
			double fTemp86 = fTemp84 * (1.0 - fTemp85) + fRec49[1] * fTemp85;
			fRec49[0] = ((std::fabs(fTemp86) > 2.2250738585072014e-308) ? fTemp86 : 0.0);
			double fTemp87 = fTemp2 - fConst162 * (fConst163 * fRec52[2] + fConst164 * fRec52[1]);
			fRec52[0] = ((std::fabs(fTemp87) > 2.2250738585072014e-308) ? fTemp87 : 0.0);
			double fTemp88 = std::fabs(fConst161 * (fRec52[0] - fRec52[2]));
			double fTemp89 = ((fTemp88 > fRec51[1]) ? fConst17 : fConst16);
			double fTemp90 = fTemp88 * (1.0 - fTemp89) + fRec51[1] * fTemp89;
			fRec51[0] = ((std::fabs(fTemp90) > 2.2250738585072014e-308) ? fTemp90 : 0.0);
			double fTemp91 = fTemp2 - fConst169 * (fConst170 * fRec54[2] + fConst171 * fRec54[1]);
			fRec54[0] = ((std::fabs(fTemp91) > 2.2250738585072014e-308) ? fTemp91 : 0.0);
			double fTemp92 = std::fabs(fConst168 * (fRec54[0] - fRec54[2]));
			double fTemp93 = ((fTemp92 > fRec53[1]) ? fConst17 : fConst16);
			double fTemp94 = fTemp92 * (1.0 - fTemp93) + fRec53[1] * fTemp93;
			fRec53[0] = ((std::fabs(fTemp94) > 2.2250738585072014e-308) ? fTemp94 : 0.0);
			double fTemp95 = fTemp2 - fConst176 * (fConst177 * fRec56[2] + fConst178 * fRec56[1]);
			fRec56[0] = ((std::fabs(fTemp95) > 2.2250738585072014e-308) ? fTemp95 : 0.0);
			double fTemp96 = std::fabs(fConst175 * (fRec56[0] - fRec56[2]));
			double fTemp97 = ((fTemp96 > fRec55[1]) ? fConst17 : fConst16);
			double fTemp98 = fTemp96 * (1.0 - fTemp97) + fRec55[1] * fTemp97;
			fRec55[0] = ((std::fabs(fTemp98) > 2.2250738585072014e-308) ? fTemp98 : 0.0);
			double fTemp99 = fTemp2 - fConst183 * (fConst184 * fRec58[2] + fConst185 * fRec58[1]);
			fRec58[0] = ((std::fabs(fTemp99) > 2.2250738585072014e-308) ? fTemp99 : 0.0);
			double fTemp100 = std::fabs(fConst182 * (fRec58[0] - fRec58[2]));
			double fTemp101 = ((fTemp100 > fRec57[1]) ? fConst17 : fConst16);
			double fTemp102 = fTemp100 * (1.0 - fTemp101) + fRec57[1] * fTemp101;
			fRec57[0] = ((std::fabs(fTemp102) > 2.2250738585072014e-308) ? fTemp102 : 0.0);
			double fTemp103 = fTemp2 - fConst190 * (fConst191 * fRec60[2] + fConst192 * fRec60[1]);
			fRec60[0] = ((std::fabs(fTemp103) > 2.2250738585072014e-308) ? fTemp103 : 0.0);
			double fTemp104 = std::fabs(fConst189 * (fRec60[0] - fRec60[2]));
			double fTemp105 = ((fTemp104 > fRec59[1]) ? fConst17 : fConst16);
			double fTemp106 = fTemp104 * (1.0 - fTemp105) + fRec59[1] * fTemp105;
			fRec59[0] = ((std::fabs(fTemp106) > 2.2250738585072014e-308) ? fTemp106 : 0.0);
			double fTemp107 = fTemp2 - fConst197 * (fConst198 * fRec62[2] + fConst199 * fRec62[1]);
			fRec62[0] = ((std::fabs(fTemp107) > 2.2250738585072014e-308) ? fTemp107 : 0.0);
			double fTemp108 = std::fabs(fConst196 * (fRec62[0] - fRec62[2]));
			double fTemp109 = ((fTemp108 > fRec61[1]) ? fConst17 : fConst16);
			double fTemp110 = fTemp108 * (1.0 - fTemp109) + fRec61[1] * fTemp109;
			fRec61[0] = ((std::fabs(fTemp110) > 2.2250738585072014e-308) ? fTemp110 : 0.0);
			double fTemp111 = fTemp2 - fConst204 * (fConst205 * fRec64[2] + fConst206 * fRec64[1]);
			fRec64[0] = ((std::fabs(fTemp111) > 2.2250738585072014e-308) ? fTemp111 : 0.0);
			double fTemp112 = std::fabs(fConst203 * (fRec64[0] - fRec64[2]));
			double fTemp113 = ((fTemp112 > fRec63[1]) ? fConst17 : fConst16);
			double fTemp114 = fTemp112 * (1.0 - fTemp113) + fRec63[1] * fTemp113;
			fRec63[0] = ((std::fabs(fTemp114) > 2.2250738585072014e-308) ? fTemp114 : 0.0);
			double fTemp115 = fTemp2 - fConst211 * (fConst212 * fRec66[2] + fConst213 * fRec66[1]);
			fRec66[0] = ((std::fabs(fTemp115) > 2.2250738585072014e-308) ? fTemp115 : 0.0);
			double fTemp116 = std::fabs(fConst210 * (fRec66[0] - fRec66[2]));
			double fTemp117 = ((fTemp116 > fRec65[1]) ? fConst17 : fConst16);
			double fTemp118 = fTemp116 * (1.0 - fTemp117) + fRec65[1] * fTemp117;
			fRec65[0] = ((std::fabs(fTemp118) > 2.2250738585072014e-308) ? fTemp118 : 0.0);
			double fTemp119 = fTemp2 - fConst218 * (fConst219 * fRec68[2] + fConst220 * fRec68[1]);
			fRec68[0] = ((std::fabs(fTemp119) > 2.2250738585072014e-308) ? fTemp119 : 0.0);
			double fTemp120 = std::fabs(fConst217 * (fRec68[0] - fRec68[2]));
			double fTemp121 = ((fTemp120 > fRec67[1]) ? fConst17 : fConst16);
			double fTemp122 = fTemp120 * (1.0 - fTemp121) + fRec67[1] * fTemp121;
			fRec67[0] = ((std::fabs(fTemp122) > 2.2250738585072014e-308) ? fTemp122 : 0.0);
			double fTemp123 = fTemp2 - fConst225 * (fConst226 * fRec70[2] + fConst227 * fRec70[1]);
			fRec70[0] = ((std::fabs(fTemp123) > 2.2250738585072014e-308) ? fTemp123 : 0.0);
			double fTemp124 = std::fabs(fConst224 * (fRec70[0] - fRec70[2]));
			double fTemp125 = ((fTemp124 > fRec69[1]) ? fConst17 : fConst16);
			double fTemp126 = fTemp124 * (1.0 - fTemp125) + fRec69[1] * fTemp125;
			fRec69[0] = ((std::fabs(fTemp126) > 2.2250738585072014e-308) ? fTemp126 : 0.0);
			double fTemp127 = fTemp2 - fConst232 * (fConst233 * fRec72[2] + fConst234 * fRec72[1]);
			fRec72[0] = ((std::fabs(fTemp127) > 2.2250738585072014e-308) ? fTemp127 : 0.0);
			double fTemp128 = std::fabs(fConst231 * (fRec72[0] - fRec72[2]));
			double fTemp129 = ((fTemp128 > fRec71[1]) ? fConst17 : fConst16);
			double fTemp130 = fTemp128 * (1.0 - fTemp129) + fRec71[1] * fTemp129;
			fRec71[0] = ((std::fabs(fTemp130) > 2.2250738585072014e-308) ? fTemp130 : 0.0);
			int iTemp131 = fRec69[0] > fRec71[0];
			double fTemp132 = ((iTemp131) ? fRec69[0] : fRec71[0]);
			int iTemp133 = fRec67[0] > fTemp132;
			double fTemp134 = ((iTemp133) ? fRec67[0] : fTemp132);
			int iTemp135 = fRec65[0] > fTemp134;
			double fTemp136 = ((iTemp135) ? fRec65[0] : fTemp134);
			int iTemp137 = fRec63[0] > fTemp136;
			double fTemp138 = ((iTemp137) ? fRec63[0] : fTemp136);
			int iTemp139 = fRec61[0] > fTemp138;
			double fTemp140 = ((iTemp139) ? fRec61[0] : fTemp138);
			int iTemp141 = fRec59[0] > fTemp140;
			double fTemp142 = ((iTemp141) ? fRec59[0] : fTemp140);
			int iTemp143 = fRec57[0] > fTemp142;
			double fTemp144 = ((iTemp143) ? fRec57[0] : fTemp142);
			int iTemp145 = fRec55[0] > fTemp144;
			double fTemp146 = ((iTemp145) ? fRec55[0] : fTemp144);
			int iTemp147 = fRec53[0] > fTemp146;
			double fTemp148 = ((iTemp147) ? fRec53[0] : fTemp146);
			int iTemp149 = fRec51[0] > fTemp148;
			double fTemp150 = ((iTemp149) ? fRec51[0] : fTemp148);
			int iTemp151 = fRec49[0] > fTemp150;
			double fTemp152 = ((iTemp151) ? fRec49[0] : fTemp150);
			int iTemp153 = fRec47[0] > fTemp152;
			double fTemp154 = ((iTemp153) ? fRec47[0] : fTemp152);
			int iTemp155 = fRec45[0] > fTemp154;
			double fTemp156 = ((iTemp155) ? fRec45[0] : fTemp154);
			int iTemp157 = fRec43[0] > fTemp156;
			double fTemp158 = ((iTemp157) ? fRec43[0] : fTemp156);
			int iTemp159 = fRec41[0] > fTemp158;
			double fTemp160 = ((iTemp159) ? fRec41[0] : fTemp158);
			int iTemp161 = fRec39[0] > fTemp160;
			double fTemp162 = ((iTemp161) ? fRec39[0] : fTemp160);
			int iTemp163 = fRec37[0] > fTemp162;
			double fTemp164 = ((iTemp163) ? fRec37[0] : fTemp162);
			int iTemp165 = fRec35[0] > fTemp164;
			double fTemp166 = ((iTemp165) ? fRec35[0] : fTemp164);
			int iTemp167 = fRec33[0] > fTemp166;
			double fTemp168 = ((iTemp167) ? fRec33[0] : fTemp166);
			int iTemp169 = fRec31[0] > fTemp168;
			double fTemp170 = ((iTemp169) ? fRec31[0] : fTemp168);
			int iTemp171 = fRec29[0] > fTemp170;
			double fTemp172 = ((iTemp171) ? fRec29[0] : fTemp170);
			int iTemp173 = fRec27[0] > fTemp172;
			double fTemp174 = ((iTemp173) ? fRec27[0] : fTemp172);
			int iTemp175 = fRec25[0] > fTemp174;
			double fTemp176 = ((iTemp175) ? fRec25[0] : fTemp174);
			int iTemp177 = fRec23[0] > fTemp176;
			double fTemp178 = ((iTemp177) ? fRec23[0] : fTemp176);
			int iTemp179 = fRec21[0] > fTemp178;
			double fTemp180 = ((iTemp179) ? fRec21[0] : fTemp178);
			int iTemp181 = fRec19[0] > fTemp180;
			double fTemp182 = ((iTemp181) ? fRec19[0] : fTemp180);
			int iTemp183 = fRec17[0] > fTemp182;
			double fTemp184 = ((iTemp183) ? fRec17[0] : fTemp182);
			int iTemp185 = fRec15[0] > fTemp184;
			double fTemp186 = ((iTemp185) ? fRec15[0] : fTemp184);
			int iTemp187 = fRec13[0] > fTemp186;
			double fTemp188 = ((iTemp187) ? fRec13[0] : fTemp186);
			int iTemp189 = fRec11[0] > fTemp188;
			double fTemp190 = ((iTemp189) ? fRec11[0] : fTemp188);
			int iTemp191 = fRec9[0] > fTemp190;
			double fTemp192 = static_cast<double>(std::abs((2e+01 * std::log10((1e-09 + ((iTemp191) ? fRec9[0] : fTemp190)) / (1e-09 + 0.03125 * (fRec71[0] + fRec69[0] + fRec67[0] + fRec65[0] + fRec63[0] + fRec61[0] + fRec59[0] + fRec57[0] + fRec55[0] + fRec53[0] + fRec51[0] + fRec49[0] + fRec47[0] + fRec45[0] + fRec43[0] + fRec41[0] + fRec39[0] + fRec37[0] + fRec35[0] + fRec33[0] + fRec31[0] + fRec29[0] + fRec27[0] + fRec25[0] + fRec23[0] + fRec21[0] + fRec19[0] + fRec17[0] + fRec15[0] + fRec13[0] + fRec9[0] + fRec11[0])))) > fSlow7));
			double fTemp193 = ((fTemp192 > fRec8[1]) ? fSlow13 : fSlow11);
			double fTemp194 = fTemp192 * (1.0 - fTemp193) + fRec8[1] * fTemp193;
			fRec8[0] = ((std::fabs(fTemp194) > 2.2250738585072014e-308) ? fTemp194 : 0.0);
			double fTemp195 = ((fRec8[0] > 0.5) ? ((iTemp191) ? 6e+03 : ((iTemp189) ? 5326.86751846578 : ((iTemp187) ? 4729.252926547629 : ((iTemp185) ? 4198.6839668392995 : ((iTemp183) ? 3727.638873876553 : ((iTemp181) ? 3309.4397396372287 : ((iTemp179) ? 2938.1578422322327 : ((iTemp177) ? 2608.529595652064 : ((iTemp175) ? 2315.8819290059428 : ((iTemp173) ? 2056.0660373706055 : ((iTemp171) ? 1825.398565048354 : ((iTemp169) ? 1620.609387401687 : ((iTemp167) ? 1438.7952509784623 : ((iTemp165) ? 1277.3786146933319 : ((iTemp163) ? 1134.0711085654536 : ((iTemp161) ? 1006.8410919746325 : ((iTemp159) ? 893.884851516048 : ((iTemp157) ? 793.6010301315737 : ((iTemp155) ? 704.5679250048105 : ((iTemp153) ? 625.5233323768265 : ((iTemp151) ? 555.3466535467651 : ((iTemp149) ? 493.043008377822 : ((iTemp147) ? 437.7291310890786 : ((iTemp145) ? 388.620848380777 : ((iTemp143) ? 345.02196237302934 : ((iTemp141) ? 306.3143807537021 : ((iTemp139) ? 271.9493542126425 : ((iTemp137) ? 241.4396969371784 : ((iTemp135) ? 214.3528798804796 : ((iTemp133) ? 190.3048988874873 : ((iTemp131) ? 168.95483074811176 : 1.5e+02))))))))))))))))))))))))))))))) : fRec7[1]);
			fRec7[0] = ((std::fabs(fTemp195) > 2.2250738585072014e-308) ? fTemp195 : 0.0);
			double fTemp196 = fConst8 * std::min<double>(6e+03, std::max<double>(1.5e+02, fRec7[0])) + fConst7 * fRec6[1];
			fRec6[0] = ((std::fabs(fTemp196) > 2.2250738585072014e-308) ? fTemp196 : 0.0);
			double fTemp197 = fRec5[1] * std::cos(fConst6 * fRec6[0]);
			double fTemp198 = fTemp2 - fConst4 * fRec5[2] + fConst5 * fTemp197;
			fRec5[0] = ((std::fabs(fTemp198) > 2.2250738585072014e-308) ? fTemp198 : 0.0);
			double fTemp199 = fSlow14 * std::max<double>(0.0, std::min<double>(1.0, fRec8[0])) + fConst236 * fRec73[1];
			fRec73[0] = ((std::fabs(fTemp199) > 2.2250738585072014e-308) ? fTemp199 : 0.0);
			double fTemp200 = (0.5 * fRec5[0] - fTemp197 + 0.5 * fRec5[2]) * fRec73[0];
			double fTemp201 = fTemp2 * (1.0 - fRec73[0]);
			double fTemp202 = fConst5 * fTemp200 + fTemp201;
			double fTemp203 = fTemp202 - fConst13 * (fConst14 * fRec77[2] + fConst15 * fRec77[1]);
			fRec77[0] = ((std::fabs(fTemp203) > 2.2250738585072014e-308) ? fTemp203 : 0.0);
			double fTemp204 = std::fabs(fConst12 * (fRec77[0] - fRec77[2]));
			double fTemp205 = ((fTemp204 > fRec76[1]) ? fConst17 : fConst16);
			double fTemp206 = fTemp204 * (1.0 - fTemp205) + fRec76[1] * fTemp205;
			fRec76[0] = ((std::fabs(fTemp206) > 2.2250738585072014e-308) ? fTemp206 : 0.0);
			double fTemp207 = fTemp202 - fConst22 * (fConst23 * fRec79[2] + fConst24 * fRec79[1]);
			fRec79[0] = ((std::fabs(fTemp207) > 2.2250738585072014e-308) ? fTemp207 : 0.0);
			double fTemp208 = std::fabs(fConst21 * (fRec79[0] - fRec79[2]));
			double fTemp209 = ((fTemp208 > fRec78[1]) ? fConst17 : fConst16);
			double fTemp210 = fTemp208 * (1.0 - fTemp209) + fRec78[1] * fTemp209;
			fRec78[0] = ((std::fabs(fTemp210) > 2.2250738585072014e-308) ? fTemp210 : 0.0);
			double fTemp211 = fTemp202 - fConst29 * (fConst30 * fRec81[2] + fConst31 * fRec81[1]);
			fRec81[0] = ((std::fabs(fTemp211) > 2.2250738585072014e-308) ? fTemp211 : 0.0);
			double fTemp212 = std::fabs(fConst28 * (fRec81[0] - fRec81[2]));
			double fTemp213 = ((fTemp212 > fRec80[1]) ? fConst17 : fConst16);
			double fTemp214 = fTemp212 * (1.0 - fTemp213) + fRec80[1] * fTemp213;
			fRec80[0] = ((std::fabs(fTemp214) > 2.2250738585072014e-308) ? fTemp214 : 0.0);
			double fTemp215 = fTemp202 - fConst36 * (fConst37 * fRec83[2] + fConst38 * fRec83[1]);
			fRec83[0] = ((std::fabs(fTemp215) > 2.2250738585072014e-308) ? fTemp215 : 0.0);
			double fTemp216 = std::fabs(fConst35 * (fRec83[0] - fRec83[2]));
			double fTemp217 = ((fTemp216 > fRec82[1]) ? fConst17 : fConst16);
			double fTemp218 = fTemp216 * (1.0 - fTemp217) + fRec82[1] * fTemp217;
			fRec82[0] = ((std::fabs(fTemp218) > 2.2250738585072014e-308) ? fTemp218 : 0.0);
			double fTemp219 = fTemp202 - fConst43 * (fConst44 * fRec85[2] + fConst45 * fRec85[1]);
			fRec85[0] = ((std::fabs(fTemp219) > 2.2250738585072014e-308) ? fTemp219 : 0.0);
			double fTemp220 = std::fabs(fConst42 * (fRec85[0] - fRec85[2]));
			double fTemp221 = ((fTemp220 > fRec84[1]) ? fConst17 : fConst16);
			double fTemp222 = fTemp220 * (1.0 - fTemp221) + fRec84[1] * fTemp221;
			fRec84[0] = ((std::fabs(fTemp222) > 2.2250738585072014e-308) ? fTemp222 : 0.0);
			double fTemp223 = fTemp202 - fConst50 * (fConst51 * fRec87[2] + fConst52 * fRec87[1]);
			fRec87[0] = ((std::fabs(fTemp223) > 2.2250738585072014e-308) ? fTemp223 : 0.0);
			double fTemp224 = std::fabs(fConst49 * (fRec87[0] - fRec87[2]));
			double fTemp225 = ((fTemp224 > fRec86[1]) ? fConst17 : fConst16);
			double fTemp226 = fTemp224 * (1.0 - fTemp225) + fRec86[1] * fTemp225;
			fRec86[0] = ((std::fabs(fTemp226) > 2.2250738585072014e-308) ? fTemp226 : 0.0);
			double fTemp227 = fTemp202 - fConst57 * (fConst58 * fRec89[2] + fConst59 * fRec89[1]);
			fRec89[0] = ((std::fabs(fTemp227) > 2.2250738585072014e-308) ? fTemp227 : 0.0);
			double fTemp228 = std::fabs(fConst56 * (fRec89[0] - fRec89[2]));
			double fTemp229 = ((fTemp228 > fRec88[1]) ? fConst17 : fConst16);
			double fTemp230 = fTemp228 * (1.0 - fTemp229) + fRec88[1] * fTemp229;
			fRec88[0] = ((std::fabs(fTemp230) > 2.2250738585072014e-308) ? fTemp230 : 0.0);
			double fTemp231 = fTemp202 - fConst64 * (fConst65 * fRec91[2] + fConst66 * fRec91[1]);
			fRec91[0] = ((std::fabs(fTemp231) > 2.2250738585072014e-308) ? fTemp231 : 0.0);
			double fTemp232 = std::fabs(fConst63 * (fRec91[0] - fRec91[2]));
			double fTemp233 = ((fTemp232 > fRec90[1]) ? fConst17 : fConst16);
			double fTemp234 = fTemp232 * (1.0 - fTemp233) + fRec90[1] * fTemp233;
			fRec90[0] = ((std::fabs(fTemp234) > 2.2250738585072014e-308) ? fTemp234 : 0.0);
			double fTemp235 = fTemp202 - fConst71 * (fConst72 * fRec93[2] + fConst73 * fRec93[1]);
			fRec93[0] = ((std::fabs(fTemp235) > 2.2250738585072014e-308) ? fTemp235 : 0.0);
			double fTemp236 = std::fabs(fConst70 * (fRec93[0] - fRec93[2]));
			double fTemp237 = ((fTemp236 > fRec92[1]) ? fConst17 : fConst16);
			double fTemp238 = fTemp236 * (1.0 - fTemp237) + fRec92[1] * fTemp237;
			fRec92[0] = ((std::fabs(fTemp238) > 2.2250738585072014e-308) ? fTemp238 : 0.0);
			double fTemp239 = fTemp202 - fConst78 * (fConst79 * fRec95[2] + fConst80 * fRec95[1]);
			fRec95[0] = ((std::fabs(fTemp239) > 2.2250738585072014e-308) ? fTemp239 : 0.0);
			double fTemp240 = std::fabs(fConst77 * (fRec95[0] - fRec95[2]));
			double fTemp241 = ((fTemp240 > fRec94[1]) ? fConst17 : fConst16);
			double fTemp242 = fTemp240 * (1.0 - fTemp241) + fRec94[1] * fTemp241;
			fRec94[0] = ((std::fabs(fTemp242) > 2.2250738585072014e-308) ? fTemp242 : 0.0);
			double fTemp243 = fTemp202 - fConst85 * (fConst86 * fRec97[2] + fConst87 * fRec97[1]);
			fRec97[0] = ((std::fabs(fTemp243) > 2.2250738585072014e-308) ? fTemp243 : 0.0);
			double fTemp244 = std::fabs(fConst84 * (fRec97[0] - fRec97[2]));
			double fTemp245 = ((fTemp244 > fRec96[1]) ? fConst17 : fConst16);
			double fTemp246 = fTemp244 * (1.0 - fTemp245) + fRec96[1] * fTemp245;
			fRec96[0] = ((std::fabs(fTemp246) > 2.2250738585072014e-308) ? fTemp246 : 0.0);
			double fTemp247 = fTemp202 - fConst92 * (fConst93 * fRec99[2] + fConst94 * fRec99[1]);
			fRec99[0] = ((std::fabs(fTemp247) > 2.2250738585072014e-308) ? fTemp247 : 0.0);
			double fTemp248 = std::fabs(fConst91 * (fRec99[0] - fRec99[2]));
			double fTemp249 = ((fTemp248 > fRec98[1]) ? fConst17 : fConst16);
			double fTemp250 = fTemp248 * (1.0 - fTemp249) + fRec98[1] * fTemp249;
			fRec98[0] = ((std::fabs(fTemp250) > 2.2250738585072014e-308) ? fTemp250 : 0.0);
			double fTemp251 = fTemp202 - fConst99 * (fConst100 * fRec101[2] + fConst101 * fRec101[1]);
			fRec101[0] = ((std::fabs(fTemp251) > 2.2250738585072014e-308) ? fTemp251 : 0.0);
			double fTemp252 = std::fabs(fConst98 * (fRec101[0] - fRec101[2]));
			double fTemp253 = ((fTemp252 > fRec100[1]) ? fConst17 : fConst16);
			double fTemp254 = fTemp252 * (1.0 - fTemp253) + fRec100[1] * fTemp253;
			fRec100[0] = ((std::fabs(fTemp254) > 2.2250738585072014e-308) ? fTemp254 : 0.0);
			double fTemp255 = fTemp202 - fConst106 * (fConst107 * fRec103[2] + fConst108 * fRec103[1]);
			fRec103[0] = ((std::fabs(fTemp255) > 2.2250738585072014e-308) ? fTemp255 : 0.0);
			double fTemp256 = std::fabs(fConst105 * (fRec103[0] - fRec103[2]));
			double fTemp257 = ((fTemp256 > fRec102[1]) ? fConst17 : fConst16);
			double fTemp258 = fTemp256 * (1.0 - fTemp257) + fRec102[1] * fTemp257;
			fRec102[0] = ((std::fabs(fTemp258) > 2.2250738585072014e-308) ? fTemp258 : 0.0);
			double fTemp259 = fTemp202 - fConst113 * (fConst114 * fRec105[2] + fConst115 * fRec105[1]);
			fRec105[0] = ((std::fabs(fTemp259) > 2.2250738585072014e-308) ? fTemp259 : 0.0);
			double fTemp260 = std::fabs(fConst112 * (fRec105[0] - fRec105[2]));
			double fTemp261 = ((fTemp260 > fRec104[1]) ? fConst17 : fConst16);
			double fTemp262 = fTemp260 * (1.0 - fTemp261) + fRec104[1] * fTemp261;
			fRec104[0] = ((std::fabs(fTemp262) > 2.2250738585072014e-308) ? fTemp262 : 0.0);
			double fTemp263 = fTemp202 - fConst120 * (fConst121 * fRec107[2] + fConst122 * fRec107[1]);
			fRec107[0] = ((std::fabs(fTemp263) > 2.2250738585072014e-308) ? fTemp263 : 0.0);
			double fTemp264 = std::fabs(fConst119 * (fRec107[0] - fRec107[2]));
			double fTemp265 = ((fTemp264 > fRec106[1]) ? fConst17 : fConst16);
			double fTemp266 = fTemp264 * (1.0 - fTemp265) + fRec106[1] * fTemp265;
			fRec106[0] = ((std::fabs(fTemp266) > 2.2250738585072014e-308) ? fTemp266 : 0.0);
			double fTemp267 = fTemp202 - fConst127 * (fConst128 * fRec109[2] + fConst129 * fRec109[1]);
			fRec109[0] = ((std::fabs(fTemp267) > 2.2250738585072014e-308) ? fTemp267 : 0.0);
			double fTemp268 = std::fabs(fConst126 * (fRec109[0] - fRec109[2]));
			double fTemp269 = ((fTemp268 > fRec108[1]) ? fConst17 : fConst16);
			double fTemp270 = fTemp268 * (1.0 - fTemp269) + fRec108[1] * fTemp269;
			fRec108[0] = ((std::fabs(fTemp270) > 2.2250738585072014e-308) ? fTemp270 : 0.0);
			double fTemp271 = fTemp202 - fConst134 * (fConst135 * fRec111[2] + fConst136 * fRec111[1]);
			fRec111[0] = ((std::fabs(fTemp271) > 2.2250738585072014e-308) ? fTemp271 : 0.0);
			double fTemp272 = std::fabs(fConst133 * (fRec111[0] - fRec111[2]));
			double fTemp273 = ((fTemp272 > fRec110[1]) ? fConst17 : fConst16);
			double fTemp274 = fTemp272 * (1.0 - fTemp273) + fRec110[1] * fTemp273;
			fRec110[0] = ((std::fabs(fTemp274) > 2.2250738585072014e-308) ? fTemp274 : 0.0);
			double fTemp275 = fTemp202 - fConst141 * (fConst142 * fRec113[2] + fConst143 * fRec113[1]);
			fRec113[0] = ((std::fabs(fTemp275) > 2.2250738585072014e-308) ? fTemp275 : 0.0);
			double fTemp276 = std::fabs(fConst140 * (fRec113[0] - fRec113[2]));
			double fTemp277 = ((fTemp276 > fRec112[1]) ? fConst17 : fConst16);
			double fTemp278 = fTemp276 * (1.0 - fTemp277) + fRec112[1] * fTemp277;
			fRec112[0] = ((std::fabs(fTemp278) > 2.2250738585072014e-308) ? fTemp278 : 0.0);
			double fTemp279 = fTemp202 - fConst148 * (fConst149 * fRec115[2] + fConst150 * fRec115[1]);
			fRec115[0] = ((std::fabs(fTemp279) > 2.2250738585072014e-308) ? fTemp279 : 0.0);
			double fTemp280 = std::fabs(fConst147 * (fRec115[0] - fRec115[2]));
			double fTemp281 = ((fTemp280 > fRec114[1]) ? fConst17 : fConst16);
			double fTemp282 = fTemp280 * (1.0 - fTemp281) + fRec114[1] * fTemp281;
			fRec114[0] = ((std::fabs(fTemp282) > 2.2250738585072014e-308) ? fTemp282 : 0.0);
			double fTemp283 = fTemp202 - fConst155 * (fConst156 * fRec117[2] + fConst157 * fRec117[1]);
			fRec117[0] = ((std::fabs(fTemp283) > 2.2250738585072014e-308) ? fTemp283 : 0.0);
			double fTemp284 = std::fabs(fConst154 * (fRec117[0] - fRec117[2]));
			double fTemp285 = ((fTemp284 > fRec116[1]) ? fConst17 : fConst16);
			double fTemp286 = fTemp284 * (1.0 - fTemp285) + fRec116[1] * fTemp285;
			fRec116[0] = ((std::fabs(fTemp286) > 2.2250738585072014e-308) ? fTemp286 : 0.0);
			double fTemp287 = fTemp202 - fConst162 * (fConst163 * fRec119[2] + fConst164 * fRec119[1]);
			fRec119[0] = ((std::fabs(fTemp287) > 2.2250738585072014e-308) ? fTemp287 : 0.0);
			double fTemp288 = std::fabs(fConst161 * (fRec119[0] - fRec119[2]));
			double fTemp289 = ((fTemp288 > fRec118[1]) ? fConst17 : fConst16);
			double fTemp290 = fTemp288 * (1.0 - fTemp289) + fRec118[1] * fTemp289;
			fRec118[0] = ((std::fabs(fTemp290) > 2.2250738585072014e-308) ? fTemp290 : 0.0);
			double fTemp291 = fTemp202 - fConst169 * (fConst170 * fRec121[2] + fConst171 * fRec121[1]);
			fRec121[0] = ((std::fabs(fTemp291) > 2.2250738585072014e-308) ? fTemp291 : 0.0);
			double fTemp292 = std::fabs(fConst168 * (fRec121[0] - fRec121[2]));
			double fTemp293 = ((fTemp292 > fRec120[1]) ? fConst17 : fConst16);
			double fTemp294 = fTemp292 * (1.0 - fTemp293) + fRec120[1] * fTemp293;
			fRec120[0] = ((std::fabs(fTemp294) > 2.2250738585072014e-308) ? fTemp294 : 0.0);
			double fTemp295 = fTemp202 - fConst176 * (fConst177 * fRec123[2] + fConst178 * fRec123[1]);
			fRec123[0] = ((std::fabs(fTemp295) > 2.2250738585072014e-308) ? fTemp295 : 0.0);
			double fTemp296 = std::fabs(fConst175 * (fRec123[0] - fRec123[2]));
			double fTemp297 = ((fTemp296 > fRec122[1]) ? fConst17 : fConst16);
			double fTemp298 = fTemp296 * (1.0 - fTemp297) + fRec122[1] * fTemp297;
			fRec122[0] = ((std::fabs(fTemp298) > 2.2250738585072014e-308) ? fTemp298 : 0.0);
			double fTemp299 = fTemp202 - fConst183 * (fConst184 * fRec125[2] + fConst185 * fRec125[1]);
			fRec125[0] = ((std::fabs(fTemp299) > 2.2250738585072014e-308) ? fTemp299 : 0.0);
			double fTemp300 = std::fabs(fConst182 * (fRec125[0] - fRec125[2]));
			double fTemp301 = ((fTemp300 > fRec124[1]) ? fConst17 : fConst16);
			double fTemp302 = fTemp300 * (1.0 - fTemp301) + fRec124[1] * fTemp301;
			fRec124[0] = ((std::fabs(fTemp302) > 2.2250738585072014e-308) ? fTemp302 : 0.0);
			double fTemp303 = fTemp202 - fConst190 * (fConst191 * fRec127[2] + fConst192 * fRec127[1]);
			fRec127[0] = ((std::fabs(fTemp303) > 2.2250738585072014e-308) ? fTemp303 : 0.0);
			double fTemp304 = std::fabs(fConst189 * (fRec127[0] - fRec127[2]));
			double fTemp305 = ((fTemp304 > fRec126[1]) ? fConst17 : fConst16);
			double fTemp306 = fTemp304 * (1.0 - fTemp305) + fRec126[1] * fTemp305;
			fRec126[0] = ((std::fabs(fTemp306) > 2.2250738585072014e-308) ? fTemp306 : 0.0);
			double fTemp307 = fTemp202 - fConst197 * (fConst198 * fRec129[2] + fConst199 * fRec129[1]);
			fRec129[0] = ((std::fabs(fTemp307) > 2.2250738585072014e-308) ? fTemp307 : 0.0);
			double fTemp308 = std::fabs(fConst196 * (fRec129[0] - fRec129[2]));
			double fTemp309 = ((fTemp308 > fRec128[1]) ? fConst17 : fConst16);
			double fTemp310 = fTemp308 * (1.0 - fTemp309) + fRec128[1] * fTemp309;
			fRec128[0] = ((std::fabs(fTemp310) > 2.2250738585072014e-308) ? fTemp310 : 0.0);
			double fTemp311 = fTemp202 - fConst204 * (fConst205 * fRec131[2] + fConst206 * fRec131[1]);
			fRec131[0] = ((std::fabs(fTemp311) > 2.2250738585072014e-308) ? fTemp311 : 0.0);
			double fTemp312 = std::fabs(fConst203 * (fRec131[0] - fRec131[2]));
			double fTemp313 = ((fTemp312 > fRec130[1]) ? fConst17 : fConst16);
			double fTemp314 = fTemp312 * (1.0 - fTemp313) + fRec130[1] * fTemp313;
			fRec130[0] = ((std::fabs(fTemp314) > 2.2250738585072014e-308) ? fTemp314 : 0.0);
			double fTemp315 = fTemp202 - fConst211 * (fConst212 * fRec133[2] + fConst213 * fRec133[1]);
			fRec133[0] = ((std::fabs(fTemp315) > 2.2250738585072014e-308) ? fTemp315 : 0.0);
			double fTemp316 = std::fabs(fConst210 * (fRec133[0] - fRec133[2]));
			double fTemp317 = ((fTemp316 > fRec132[1]) ? fConst17 : fConst16);
			double fTemp318 = fTemp316 * (1.0 - fTemp317) + fRec132[1] * fTemp317;
			fRec132[0] = ((std::fabs(fTemp318) > 2.2250738585072014e-308) ? fTemp318 : 0.0);
			double fTemp319 = fTemp202 - fConst218 * (fConst219 * fRec135[2] + fConst220 * fRec135[1]);
			fRec135[0] = ((std::fabs(fTemp319) > 2.2250738585072014e-308) ? fTemp319 : 0.0);
			double fTemp320 = std::fabs(fConst217 * (fRec135[0] - fRec135[2]));
			double fTemp321 = ((fTemp320 > fRec134[1]) ? fConst17 : fConst16);
			double fTemp322 = fTemp320 * (1.0 - fTemp321) + fRec134[1] * fTemp321;
			fRec134[0] = ((std::fabs(fTemp322) > 2.2250738585072014e-308) ? fTemp322 : 0.0);
			double fTemp323 = fTemp202 - fConst225 * (fConst226 * fRec137[2] + fConst227 * fRec137[1]);
			fRec137[0] = ((std::fabs(fTemp323) > 2.2250738585072014e-308) ? fTemp323 : 0.0);
			double fTemp324 = std::fabs(fConst224 * (fRec137[0] - fRec137[2]));
			double fTemp325 = ((fTemp324 > fRec136[1]) ? fConst17 : fConst16);
			double fTemp326 = fTemp324 * (1.0 - fTemp325) + fRec136[1] * fTemp325;
			fRec136[0] = ((std::fabs(fTemp326) > 2.2250738585072014e-308) ? fTemp326 : 0.0);
			double fTemp327 = fTemp202 - fConst232 * (fConst233 * fRec139[2] + fConst234 * fRec139[1]);
			fRec139[0] = ((std::fabs(fTemp327) > 2.2250738585072014e-308) ? fTemp327 : 0.0);
			double fTemp328 = std::fabs(fConst231 * (fRec139[0] - fRec139[2]));
			double fTemp329 = ((fTemp328 > fRec138[1]) ? fConst17 : fConst16);
			double fTemp330 = fTemp328 * (1.0 - fTemp329) + fRec138[1] * fTemp329;
			fRec138[0] = ((std::fabs(fTemp330) > 2.2250738585072014e-308) ? fTemp330 : 0.0);
			int iTemp331 = fRec136[0] > fRec138[0];
			double fTemp332 = ((iTemp331) ? fRec136[0] : fRec138[0]);
			int iTemp333 = fRec134[0] > fTemp332;
			double fTemp334 = ((iTemp333) ? fRec134[0] : fTemp332);
			int iTemp335 = fRec132[0] > fTemp334;
			double fTemp336 = ((iTemp335) ? fRec132[0] : fTemp334);
			int iTemp337 = fRec130[0] > fTemp336;
			double fTemp338 = ((iTemp337) ? fRec130[0] : fTemp336);
			int iTemp339 = fRec128[0] > fTemp338;
			double fTemp340 = ((iTemp339) ? fRec128[0] : fTemp338);
			int iTemp341 = fRec126[0] > fTemp340;
			double fTemp342 = ((iTemp341) ? fRec126[0] : fTemp340);
			int iTemp343 = fRec124[0] > fTemp342;
			double fTemp344 = ((iTemp343) ? fRec124[0] : fTemp342);
			int iTemp345 = fRec122[0] > fTemp344;
			double fTemp346 = ((iTemp345) ? fRec122[0] : fTemp344);
			int iTemp347 = fRec120[0] > fTemp346;
			double fTemp348 = ((iTemp347) ? fRec120[0] : fTemp346);
			int iTemp349 = fRec118[0] > fTemp348;
			double fTemp350 = ((iTemp349) ? fRec118[0] : fTemp348);
			int iTemp351 = fRec116[0] > fTemp350;
			double fTemp352 = ((iTemp351) ? fRec116[0] : fTemp350);
			int iTemp353 = fRec114[0] > fTemp352;
			double fTemp354 = ((iTemp353) ? fRec114[0] : fTemp352);
			int iTemp355 = fRec112[0] > fTemp354;
			double fTemp356 = ((iTemp355) ? fRec112[0] : fTemp354);
			int iTemp357 = fRec110[0] > fTemp356;
			double fTemp358 = ((iTemp357) ? fRec110[0] : fTemp356);
			int iTemp359 = fRec108[0] > fTemp358;
			double fTemp360 = ((iTemp359) ? fRec108[0] : fTemp358);
			int iTemp361 = fRec106[0] > fTemp360;
			double fTemp362 = ((iTemp361) ? fRec106[0] : fTemp360);
			int iTemp363 = fRec104[0] > fTemp362;
			double fTemp364 = ((iTemp363) ? fRec104[0] : fTemp362);
			int iTemp365 = fRec102[0] > fTemp364;
			double fTemp366 = ((iTemp365) ? fRec102[0] : fTemp364);
			int iTemp367 = fRec100[0] > fTemp366;
			double fTemp368 = ((iTemp367) ? fRec100[0] : fTemp366);
			int iTemp369 = fRec98[0] > fTemp368;
			double fTemp370 = ((iTemp369) ? fRec98[0] : fTemp368);
			int iTemp371 = fRec96[0] > fTemp370;
			double fTemp372 = ((iTemp371) ? fRec96[0] : fTemp370);
			int iTemp373 = fRec94[0] > fTemp372;
			double fTemp374 = ((iTemp373) ? fRec94[0] : fTemp372);
			int iTemp375 = fRec92[0] > fTemp374;
			double fTemp376 = ((iTemp375) ? fRec92[0] : fTemp374);
			int iTemp377 = fRec90[0] > fTemp376;
			double fTemp378 = ((iTemp377) ? fRec90[0] : fTemp376);
			int iTemp379 = fRec88[0] > fTemp378;
			double fTemp380 = ((iTemp379) ? fRec88[0] : fTemp378);
			int iTemp381 = fRec86[0] > fTemp380;
			double fTemp382 = ((iTemp381) ? fRec86[0] : fTemp380);
			int iTemp383 = fRec84[0] > fTemp382;
			double fTemp384 = ((iTemp383) ? fRec84[0] : fTemp382);
			int iTemp385 = fRec82[0] > fTemp384;
			double fTemp386 = ((iTemp385) ? fRec82[0] : fTemp384);
			int iTemp387 = fRec80[0] > fTemp386;
			double fTemp388 = ((iTemp387) ? fRec80[0] : fTemp386);
			int iTemp389 = fRec78[0] > fTemp388;
			double fTemp390 = ((iTemp389) ? fRec78[0] : fTemp388);
			int iTemp391 = fRec76[0] > fTemp390;
			double fTemp392 = static_cast<double>(std::abs((2e+01 * std::log10((1e-09 + ((iTemp391) ? fRec76[0] : fTemp390)) / (1e-09 + 0.03125 * (fRec138[0] + fRec136[0] + fRec134[0] + fRec132[0] + fRec130[0] + fRec128[0] + fRec126[0] + fRec124[0] + fRec122[0] + fRec120[0] + fRec118[0] + fRec116[0] + fRec114[0] + fRec112[0] + fRec110[0] + fRec108[0] + fRec106[0] + fRec104[0] + fRec102[0] + fRec100[0] + fRec98[0] + fRec96[0] + fRec94[0] + fRec92[0] + fRec90[0] + fRec88[0] + fRec86[0] + fRec84[0] + fRec82[0] + fRec80[0] + fRec76[0] + fRec78[0])))) > fSlow7));
			double fTemp393 = ((fTemp392 > fRec75[1]) ? fSlow13 : fSlow11);
			double fTemp394 = fTemp392 * (1.0 - fTemp393) + fRec75[1] * fTemp393;
			fRec75[0] = ((std::fabs(fTemp394) > 2.2250738585072014e-308) ? fTemp394 : 0.0);
			double fTemp395 = fSlow14 * std::max<double>(0.0, std::min<double>(1.0, fRec75[0])) + fConst236 * fRec74[1];
			fRec74[0] = ((std::fabs(fTemp395) > 2.2250738585072014e-308) ? fTemp395 : 0.0);
			double fTemp396 = fTemp202 * (1.0 - fRec74[0]);
			double fTemp397 = ((fRec75[0] > 0.5) ? ((iTemp391) ? 6e+03 : ((iTemp389) ? 5326.86751846578 : ((iTemp387) ? 4729.252926547629 : ((iTemp385) ? 4198.6839668392995 : ((iTemp383) ? 3727.638873876553 : ((iTemp381) ? 3309.4397396372287 : ((iTemp379) ? 2938.1578422322327 : ((iTemp377) ? 2608.529595652064 : ((iTemp375) ? 2315.8819290059428 : ((iTemp373) ? 2056.0660373706055 : ((iTemp371) ? 1825.398565048354 : ((iTemp369) ? 1620.609387401687 : ((iTemp367) ? 1438.7952509784623 : ((iTemp365) ? 1277.3786146933319 : ((iTemp363) ? 1134.0711085654536 : ((iTemp361) ? 1006.8410919746325 : ((iTemp359) ? 893.884851516048 : ((iTemp357) ? 793.6010301315737 : ((iTemp355) ? 704.5679250048105 : ((iTemp353) ? 625.5233323768265 : ((iTemp351) ? 555.3466535467651 : ((iTemp349) ? 493.043008377822 : ((iTemp347) ? 437.7291310890786 : ((iTemp345) ? 388.620848380777 : ((iTemp343) ? 345.02196237302934 : ((iTemp341) ? 306.3143807537021 : ((iTemp339) ? 271.9493542126425 : ((iTemp337) ? 241.4396969371784 : ((iTemp335) ? 214.3528798804796 : ((iTemp333) ? 190.3048988874873 : ((iTemp331) ? 168.95483074811176 : 1.5e+02))))))))))))))))))))))))))))))) : fRec142[1]);
			fRec142[0] = ((std::fabs(fTemp397) > 2.2250738585072014e-308) ? fTemp397 : 0.0);
			double fTemp398 = fConst8 * std::min<double>(6e+03, std::max<double>(1.5e+02, fRec142[0])) + fConst7 * fRec141[1];
			fRec141[0] = ((std::fabs(fTemp398) > 2.2250738585072014e-308) ? fTemp398 : 0.0);
			double fTemp399 = fRec140[1] * std::cos(fConst6 * fRec141[0]);
			double fTemp400 = fTemp201 + fConst5 * (fTemp200 + fTemp399) - fConst4 * fRec140[2];
			fRec140[0] = ((std::fabs(fTemp400) > 2.2250738585072014e-308) ? fTemp400 : 0.0);
			double fTemp401 = (0.5 * fRec140[0] - fTemp399 + 0.5 * fRec140[2]) * fRec74[0];
			double fTemp402 = fConst5 * fTemp401 + fTemp396;
			double fTemp403 = fTemp402 - fConst13 * (fConst14 * fRec147[2] + fConst15 * fRec147[1]);
			fRec147[0] = ((std::fabs(fTemp403) > 2.2250738585072014e-308) ? fTemp403 : 0.0);
			double fTemp404 = std::fabs(fConst12 * (fRec147[0] - fRec147[2]));
			double fTemp405 = ((fTemp404 > fRec146[1]) ? fConst17 : fConst16);
			double fTemp406 = fTemp404 * (1.0 - fTemp405) + fRec146[1] * fTemp405;
			fRec146[0] = ((std::fabs(fTemp406) > 2.2250738585072014e-308) ? fTemp406 : 0.0);
			double fTemp407 = fTemp402 - fConst22 * (fConst23 * fRec149[2] + fConst24 * fRec149[1]);
			fRec149[0] = ((std::fabs(fTemp407) > 2.2250738585072014e-308) ? fTemp407 : 0.0);
			double fTemp408 = std::fabs(fConst21 * (fRec149[0] - fRec149[2]));
			double fTemp409 = ((fTemp408 > fRec148[1]) ? fConst17 : fConst16);
			double fTemp410 = fTemp408 * (1.0 - fTemp409) + fRec148[1] * fTemp409;
			fRec148[0] = ((std::fabs(fTemp410) > 2.2250738585072014e-308) ? fTemp410 : 0.0);
			double fTemp411 = fTemp402 - fConst29 * (fConst30 * fRec151[2] + fConst31 * fRec151[1]);
			fRec151[0] = ((std::fabs(fTemp411) > 2.2250738585072014e-308) ? fTemp411 : 0.0);
			double fTemp412 = std::fabs(fConst28 * (fRec151[0] - fRec151[2]));
			double fTemp413 = ((fTemp412 > fRec150[1]) ? fConst17 : fConst16);
			double fTemp414 = fTemp412 * (1.0 - fTemp413) + fRec150[1] * fTemp413;
			fRec150[0] = ((std::fabs(fTemp414) > 2.2250738585072014e-308) ? fTemp414 : 0.0);
			double fTemp415 = fTemp402 - fConst36 * (fConst37 * fRec153[2] + fConst38 * fRec153[1]);
			fRec153[0] = ((std::fabs(fTemp415) > 2.2250738585072014e-308) ? fTemp415 : 0.0);
			double fTemp416 = std::fabs(fConst35 * (fRec153[0] - fRec153[2]));
			double fTemp417 = ((fTemp416 > fRec152[1]) ? fConst17 : fConst16);
			double fTemp418 = fTemp416 * (1.0 - fTemp417) + fRec152[1] * fTemp417;
			fRec152[0] = ((std::fabs(fTemp418) > 2.2250738585072014e-308) ? fTemp418 : 0.0);
			double fTemp419 = fTemp402 - fConst43 * (fConst44 * fRec155[2] + fConst45 * fRec155[1]);
			fRec155[0] = ((std::fabs(fTemp419) > 2.2250738585072014e-308) ? fTemp419 : 0.0);
			double fTemp420 = std::fabs(fConst42 * (fRec155[0] - fRec155[2]));
			double fTemp421 = ((fTemp420 > fRec154[1]) ? fConst17 : fConst16);
			double fTemp422 = fTemp420 * (1.0 - fTemp421) + fRec154[1] * fTemp421;
			fRec154[0] = ((std::fabs(fTemp422) > 2.2250738585072014e-308) ? fTemp422 : 0.0);
			double fTemp423 = fTemp402 - fConst50 * (fConst51 * fRec157[2] + fConst52 * fRec157[1]);
			fRec157[0] = ((std::fabs(fTemp423) > 2.2250738585072014e-308) ? fTemp423 : 0.0);
			double fTemp424 = std::fabs(fConst49 * (fRec157[0] - fRec157[2]));
			double fTemp425 = ((fTemp424 > fRec156[1]) ? fConst17 : fConst16);
			double fTemp426 = fTemp424 * (1.0 - fTemp425) + fRec156[1] * fTemp425;
			fRec156[0] = ((std::fabs(fTemp426) > 2.2250738585072014e-308) ? fTemp426 : 0.0);
			double fTemp427 = fTemp402 - fConst57 * (fConst58 * fRec159[2] + fConst59 * fRec159[1]);
			fRec159[0] = ((std::fabs(fTemp427) > 2.2250738585072014e-308) ? fTemp427 : 0.0);
			double fTemp428 = std::fabs(fConst56 * (fRec159[0] - fRec159[2]));
			double fTemp429 = ((fTemp428 > fRec158[1]) ? fConst17 : fConst16);
			double fTemp430 = fTemp428 * (1.0 - fTemp429) + fRec158[1] * fTemp429;
			fRec158[0] = ((std::fabs(fTemp430) > 2.2250738585072014e-308) ? fTemp430 : 0.0);
			double fTemp431 = fTemp402 - fConst64 * (fConst65 * fRec161[2] + fConst66 * fRec161[1]);
			fRec161[0] = ((std::fabs(fTemp431) > 2.2250738585072014e-308) ? fTemp431 : 0.0);
			double fTemp432 = std::fabs(fConst63 * (fRec161[0] - fRec161[2]));
			double fTemp433 = ((fTemp432 > fRec160[1]) ? fConst17 : fConst16);
			double fTemp434 = fTemp432 * (1.0 - fTemp433) + fRec160[1] * fTemp433;
			fRec160[0] = ((std::fabs(fTemp434) > 2.2250738585072014e-308) ? fTemp434 : 0.0);
			double fTemp435 = fTemp402 - fConst71 * (fConst72 * fRec163[2] + fConst73 * fRec163[1]);
			fRec163[0] = ((std::fabs(fTemp435) > 2.2250738585072014e-308) ? fTemp435 : 0.0);
			double fTemp436 = std::fabs(fConst70 * (fRec163[0] - fRec163[2]));
			double fTemp437 = ((fTemp436 > fRec162[1]) ? fConst17 : fConst16);
			double fTemp438 = fTemp436 * (1.0 - fTemp437) + fRec162[1] * fTemp437;
			fRec162[0] = ((std::fabs(fTemp438) > 2.2250738585072014e-308) ? fTemp438 : 0.0);
			double fTemp439 = fTemp402 - fConst78 * (fConst79 * fRec165[2] + fConst80 * fRec165[1]);
			fRec165[0] = ((std::fabs(fTemp439) > 2.2250738585072014e-308) ? fTemp439 : 0.0);
			double fTemp440 = std::fabs(fConst77 * (fRec165[0] - fRec165[2]));
			double fTemp441 = ((fTemp440 > fRec164[1]) ? fConst17 : fConst16);
			double fTemp442 = fTemp440 * (1.0 - fTemp441) + fRec164[1] * fTemp441;
			fRec164[0] = ((std::fabs(fTemp442) > 2.2250738585072014e-308) ? fTemp442 : 0.0);
			double fTemp443 = fTemp402 - fConst85 * (fConst86 * fRec167[2] + fConst87 * fRec167[1]);
			fRec167[0] = ((std::fabs(fTemp443) > 2.2250738585072014e-308) ? fTemp443 : 0.0);
			double fTemp444 = std::fabs(fConst84 * (fRec167[0] - fRec167[2]));
			double fTemp445 = ((fTemp444 > fRec166[1]) ? fConst17 : fConst16);
			double fTemp446 = fTemp444 * (1.0 - fTemp445) + fRec166[1] * fTemp445;
			fRec166[0] = ((std::fabs(fTemp446) > 2.2250738585072014e-308) ? fTemp446 : 0.0);
			double fTemp447 = fTemp402 - fConst92 * (fConst93 * fRec169[2] + fConst94 * fRec169[1]);
			fRec169[0] = ((std::fabs(fTemp447) > 2.2250738585072014e-308) ? fTemp447 : 0.0);
			double fTemp448 = std::fabs(fConst91 * (fRec169[0] - fRec169[2]));
			double fTemp449 = ((fTemp448 > fRec168[1]) ? fConst17 : fConst16);
			double fTemp450 = fTemp448 * (1.0 - fTemp449) + fRec168[1] * fTemp449;
			fRec168[0] = ((std::fabs(fTemp450) > 2.2250738585072014e-308) ? fTemp450 : 0.0);
			double fTemp451 = fTemp402 - fConst99 * (fConst100 * fRec171[2] + fConst101 * fRec171[1]);
			fRec171[0] = ((std::fabs(fTemp451) > 2.2250738585072014e-308) ? fTemp451 : 0.0);
			double fTemp452 = std::fabs(fConst98 * (fRec171[0] - fRec171[2]));
			double fTemp453 = ((fTemp452 > fRec170[1]) ? fConst17 : fConst16);
			double fTemp454 = fTemp452 * (1.0 - fTemp453) + fRec170[1] * fTemp453;
			fRec170[0] = ((std::fabs(fTemp454) > 2.2250738585072014e-308) ? fTemp454 : 0.0);
			double fTemp455 = fTemp402 - fConst106 * (fConst107 * fRec173[2] + fConst108 * fRec173[1]);
			fRec173[0] = ((std::fabs(fTemp455) > 2.2250738585072014e-308) ? fTemp455 : 0.0);
			double fTemp456 = std::fabs(fConst105 * (fRec173[0] - fRec173[2]));
			double fTemp457 = ((fTemp456 > fRec172[1]) ? fConst17 : fConst16);
			double fTemp458 = fTemp456 * (1.0 - fTemp457) + fRec172[1] * fTemp457;
			fRec172[0] = ((std::fabs(fTemp458) > 2.2250738585072014e-308) ? fTemp458 : 0.0);
			double fTemp459 = fTemp402 - fConst113 * (fConst114 * fRec175[2] + fConst115 * fRec175[1]);
			fRec175[0] = ((std::fabs(fTemp459) > 2.2250738585072014e-308) ? fTemp459 : 0.0);
			double fTemp460 = std::fabs(fConst112 * (fRec175[0] - fRec175[2]));
			double fTemp461 = ((fTemp460 > fRec174[1]) ? fConst17 : fConst16);
			double fTemp462 = fTemp460 * (1.0 - fTemp461) + fRec174[1] * fTemp461;
			fRec174[0] = ((std::fabs(fTemp462) > 2.2250738585072014e-308) ? fTemp462 : 0.0);
			double fTemp463 = fTemp402 - fConst120 * (fConst121 * fRec177[2] + fConst122 * fRec177[1]);
			fRec177[0] = ((std::fabs(fTemp463) > 2.2250738585072014e-308) ? fTemp463 : 0.0);
			double fTemp464 = std::fabs(fConst119 * (fRec177[0] - fRec177[2]));
			double fTemp465 = ((fTemp464 > fRec176[1]) ? fConst17 : fConst16);
			double fTemp466 = fTemp464 * (1.0 - fTemp465) + fRec176[1] * fTemp465;
			fRec176[0] = ((std::fabs(fTemp466) > 2.2250738585072014e-308) ? fTemp466 : 0.0);
			double fTemp467 = fTemp402 - fConst127 * (fConst128 * fRec179[2] + fConst129 * fRec179[1]);
			fRec179[0] = ((std::fabs(fTemp467) > 2.2250738585072014e-308) ? fTemp467 : 0.0);
			double fTemp468 = std::fabs(fConst126 * (fRec179[0] - fRec179[2]));
			double fTemp469 = ((fTemp468 > fRec178[1]) ? fConst17 : fConst16);
			double fTemp470 = fTemp468 * (1.0 - fTemp469) + fRec178[1] * fTemp469;
			fRec178[0] = ((std::fabs(fTemp470) > 2.2250738585072014e-308) ? fTemp470 : 0.0);
			double fTemp471 = fTemp402 - fConst134 * (fConst135 * fRec181[2] + fConst136 * fRec181[1]);
			fRec181[0] = ((std::fabs(fTemp471) > 2.2250738585072014e-308) ? fTemp471 : 0.0);
			double fTemp472 = std::fabs(fConst133 * (fRec181[0] - fRec181[2]));
			double fTemp473 = ((fTemp472 > fRec180[1]) ? fConst17 : fConst16);
			double fTemp474 = fTemp472 * (1.0 - fTemp473) + fRec180[1] * fTemp473;
			fRec180[0] = ((std::fabs(fTemp474) > 2.2250738585072014e-308) ? fTemp474 : 0.0);
			double fTemp475 = fTemp402 - fConst141 * (fConst142 * fRec183[2] + fConst143 * fRec183[1]);
			fRec183[0] = ((std::fabs(fTemp475) > 2.2250738585072014e-308) ? fTemp475 : 0.0);
			double fTemp476 = std::fabs(fConst140 * (fRec183[0] - fRec183[2]));
			double fTemp477 = ((fTemp476 > fRec182[1]) ? fConst17 : fConst16);
			double fTemp478 = fTemp476 * (1.0 - fTemp477) + fRec182[1] * fTemp477;
			fRec182[0] = ((std::fabs(fTemp478) > 2.2250738585072014e-308) ? fTemp478 : 0.0);
			double fTemp479 = fTemp402 - fConst148 * (fConst149 * fRec185[2] + fConst150 * fRec185[1]);
			fRec185[0] = ((std::fabs(fTemp479) > 2.2250738585072014e-308) ? fTemp479 : 0.0);
			double fTemp480 = std::fabs(fConst147 * (fRec185[0] - fRec185[2]));
			double fTemp481 = ((fTemp480 > fRec184[1]) ? fConst17 : fConst16);
			double fTemp482 = fTemp480 * (1.0 - fTemp481) + fRec184[1] * fTemp481;
			fRec184[0] = ((std::fabs(fTemp482) > 2.2250738585072014e-308) ? fTemp482 : 0.0);
			double fTemp483 = fTemp402 - fConst155 * (fConst156 * fRec187[2] + fConst157 * fRec187[1]);
			fRec187[0] = ((std::fabs(fTemp483) > 2.2250738585072014e-308) ? fTemp483 : 0.0);
			double fTemp484 = std::fabs(fConst154 * (fRec187[0] - fRec187[2]));
			double fTemp485 = ((fTemp484 > fRec186[1]) ? fConst17 : fConst16);
			double fTemp486 = fTemp484 * (1.0 - fTemp485) + fRec186[1] * fTemp485;
			fRec186[0] = ((std::fabs(fTemp486) > 2.2250738585072014e-308) ? fTemp486 : 0.0);
			double fTemp487 = fTemp402 - fConst162 * (fConst163 * fRec189[2] + fConst164 * fRec189[1]);
			fRec189[0] = ((std::fabs(fTemp487) > 2.2250738585072014e-308) ? fTemp487 : 0.0);
			double fTemp488 = std::fabs(fConst161 * (fRec189[0] - fRec189[2]));
			double fTemp489 = ((fTemp488 > fRec188[1]) ? fConst17 : fConst16);
			double fTemp490 = fTemp488 * (1.0 - fTemp489) + fRec188[1] * fTemp489;
			fRec188[0] = ((std::fabs(fTemp490) > 2.2250738585072014e-308) ? fTemp490 : 0.0);
			double fTemp491 = fTemp402 - fConst169 * (fConst170 * fRec191[2] + fConst171 * fRec191[1]);
			fRec191[0] = ((std::fabs(fTemp491) > 2.2250738585072014e-308) ? fTemp491 : 0.0);
			double fTemp492 = std::fabs(fConst168 * (fRec191[0] - fRec191[2]));
			double fTemp493 = ((fTemp492 > fRec190[1]) ? fConst17 : fConst16);
			double fTemp494 = fTemp492 * (1.0 - fTemp493) + fRec190[1] * fTemp493;
			fRec190[0] = ((std::fabs(fTemp494) > 2.2250738585072014e-308) ? fTemp494 : 0.0);
			double fTemp495 = fTemp402 - fConst176 * (fConst177 * fRec193[2] + fConst178 * fRec193[1]);
			fRec193[0] = ((std::fabs(fTemp495) > 2.2250738585072014e-308) ? fTemp495 : 0.0);
			double fTemp496 = std::fabs(fConst175 * (fRec193[0] - fRec193[2]));
			double fTemp497 = ((fTemp496 > fRec192[1]) ? fConst17 : fConst16);
			double fTemp498 = fTemp496 * (1.0 - fTemp497) + fRec192[1] * fTemp497;
			fRec192[0] = ((std::fabs(fTemp498) > 2.2250738585072014e-308) ? fTemp498 : 0.0);
			double fTemp499 = fTemp402 - fConst183 * (fConst184 * fRec195[2] + fConst185 * fRec195[1]);
			fRec195[0] = ((std::fabs(fTemp499) > 2.2250738585072014e-308) ? fTemp499 : 0.0);
			double fTemp500 = std::fabs(fConst182 * (fRec195[0] - fRec195[2]));
			double fTemp501 = ((fTemp500 > fRec194[1]) ? fConst17 : fConst16);
			double fTemp502 = fTemp500 * (1.0 - fTemp501) + fRec194[1] * fTemp501;
			fRec194[0] = ((std::fabs(fTemp502) > 2.2250738585072014e-308) ? fTemp502 : 0.0);
			double fTemp503 = fTemp402 - fConst190 * (fConst191 * fRec197[2] + fConst192 * fRec197[1]);
			fRec197[0] = ((std::fabs(fTemp503) > 2.2250738585072014e-308) ? fTemp503 : 0.0);
			double fTemp504 = std::fabs(fConst189 * (fRec197[0] - fRec197[2]));
			double fTemp505 = ((fTemp504 > fRec196[1]) ? fConst17 : fConst16);
			double fTemp506 = fTemp504 * (1.0 - fTemp505) + fRec196[1] * fTemp505;
			fRec196[0] = ((std::fabs(fTemp506) > 2.2250738585072014e-308) ? fTemp506 : 0.0);
			double fTemp507 = fTemp402 - fConst197 * (fConst198 * fRec199[2] + fConst199 * fRec199[1]);
			fRec199[0] = ((std::fabs(fTemp507) > 2.2250738585072014e-308) ? fTemp507 : 0.0);
			double fTemp508 = std::fabs(fConst196 * (fRec199[0] - fRec199[2]));
			double fTemp509 = ((fTemp508 > fRec198[1]) ? fConst17 : fConst16);
			double fTemp510 = fTemp508 * (1.0 - fTemp509) + fRec198[1] * fTemp509;
			fRec198[0] = ((std::fabs(fTemp510) > 2.2250738585072014e-308) ? fTemp510 : 0.0);
			double fTemp511 = fTemp402 - fConst204 * (fConst205 * fRec201[2] + fConst206 * fRec201[1]);
			fRec201[0] = ((std::fabs(fTemp511) > 2.2250738585072014e-308) ? fTemp511 : 0.0);
			double fTemp512 = std::fabs(fConst203 * (fRec201[0] - fRec201[2]));
			double fTemp513 = ((fTemp512 > fRec200[1]) ? fConst17 : fConst16);
			double fTemp514 = fTemp512 * (1.0 - fTemp513) + fRec200[1] * fTemp513;
			fRec200[0] = ((std::fabs(fTemp514) > 2.2250738585072014e-308) ? fTemp514 : 0.0);
			double fTemp515 = fTemp402 - fConst211 * (fConst212 * fRec203[2] + fConst213 * fRec203[1]);
			fRec203[0] = ((std::fabs(fTemp515) > 2.2250738585072014e-308) ? fTemp515 : 0.0);
			double fTemp516 = std::fabs(fConst210 * (fRec203[0] - fRec203[2]));
			double fTemp517 = ((fTemp516 > fRec202[1]) ? fConst17 : fConst16);
			double fTemp518 = fTemp516 * (1.0 - fTemp517) + fRec202[1] * fTemp517;
			fRec202[0] = ((std::fabs(fTemp518) > 2.2250738585072014e-308) ? fTemp518 : 0.0);
			double fTemp519 = fTemp402 - fConst218 * (fConst219 * fRec205[2] + fConst220 * fRec205[1]);
			fRec205[0] = ((std::fabs(fTemp519) > 2.2250738585072014e-308) ? fTemp519 : 0.0);
			double fTemp520 = std::fabs(fConst217 * (fRec205[0] - fRec205[2]));
			double fTemp521 = ((fTemp520 > fRec204[1]) ? fConst17 : fConst16);
			double fTemp522 = fTemp520 * (1.0 - fTemp521) + fRec204[1] * fTemp521;
			fRec204[0] = ((std::fabs(fTemp522) > 2.2250738585072014e-308) ? fTemp522 : 0.0);
			double fTemp523 = fTemp402 - fConst225 * (fConst226 * fRec207[2] + fConst227 * fRec207[1]);
			fRec207[0] = ((std::fabs(fTemp523) > 2.2250738585072014e-308) ? fTemp523 : 0.0);
			double fTemp524 = std::fabs(fConst224 * (fRec207[0] - fRec207[2]));
			double fTemp525 = ((fTemp524 > fRec206[1]) ? fConst17 : fConst16);
			double fTemp526 = fTemp524 * (1.0 - fTemp525) + fRec206[1] * fTemp525;
			fRec206[0] = ((std::fabs(fTemp526) > 2.2250738585072014e-308) ? fTemp526 : 0.0);
			double fTemp527 = fTemp402 - fConst232 * (fConst233 * fRec209[2] + fConst234 * fRec209[1]);
			fRec209[0] = ((std::fabs(fTemp527) > 2.2250738585072014e-308) ? fTemp527 : 0.0);
			double fTemp528 = std::fabs(fConst231 * (fRec209[0] - fRec209[2]));
			double fTemp529 = ((fTemp528 > fRec208[1]) ? fConst17 : fConst16);
			double fTemp530 = fTemp528 * (1.0 - fTemp529) + fRec208[1] * fTemp529;
			fRec208[0] = ((std::fabs(fTemp530) > 2.2250738585072014e-308) ? fTemp530 : 0.0);
			int iTemp531 = fRec206[0] > fRec208[0];
			double fTemp532 = ((iTemp531) ? fRec206[0] : fRec208[0]);
			int iTemp533 = fRec204[0] > fTemp532;
			double fTemp534 = ((iTemp533) ? fRec204[0] : fTemp532);
			int iTemp535 = fRec202[0] > fTemp534;
			double fTemp536 = ((iTemp535) ? fRec202[0] : fTemp534);
			int iTemp537 = fRec200[0] > fTemp536;
			double fTemp538 = ((iTemp537) ? fRec200[0] : fTemp536);
			int iTemp539 = fRec198[0] > fTemp538;
			double fTemp540 = ((iTemp539) ? fRec198[0] : fTemp538);
			int iTemp541 = fRec196[0] > fTemp540;
			double fTemp542 = ((iTemp541) ? fRec196[0] : fTemp540);
			int iTemp543 = fRec194[0] > fTemp542;
			double fTemp544 = ((iTemp543) ? fRec194[0] : fTemp542);
			int iTemp545 = fRec192[0] > fTemp544;
			double fTemp546 = ((iTemp545) ? fRec192[0] : fTemp544);
			int iTemp547 = fRec190[0] > fTemp546;
			double fTemp548 = ((iTemp547) ? fRec190[0] : fTemp546);
			int iTemp549 = fRec188[0] > fTemp548;
			double fTemp550 = ((iTemp549) ? fRec188[0] : fTemp548);
			int iTemp551 = fRec186[0] > fTemp550;
			double fTemp552 = ((iTemp551) ? fRec186[0] : fTemp550);
			int iTemp553 = fRec184[0] > fTemp552;
			double fTemp554 = ((iTemp553) ? fRec184[0] : fTemp552);
			int iTemp555 = fRec182[0] > fTemp554;
			double fTemp556 = ((iTemp555) ? fRec182[0] : fTemp554);
			int iTemp557 = fRec180[0] > fTemp556;
			double fTemp558 = ((iTemp557) ? fRec180[0] : fTemp556);
			int iTemp559 = fRec178[0] > fTemp558;
			double fTemp560 = ((iTemp559) ? fRec178[0] : fTemp558);
			int iTemp561 = fRec176[0] > fTemp560;
			double fTemp562 = ((iTemp561) ? fRec176[0] : fTemp560);
			int iTemp563 = fRec174[0] > fTemp562;
			double fTemp564 = ((iTemp563) ? fRec174[0] : fTemp562);
			int iTemp565 = fRec172[0] > fTemp564;
			double fTemp566 = ((iTemp565) ? fRec172[0] : fTemp564);
			int iTemp567 = fRec170[0] > fTemp566;
			double fTemp568 = ((iTemp567) ? fRec170[0] : fTemp566);
			int iTemp569 = fRec168[0] > fTemp568;
			double fTemp570 = ((iTemp569) ? fRec168[0] : fTemp568);
			int iTemp571 = fRec166[0] > fTemp570;
			double fTemp572 = ((iTemp571) ? fRec166[0] : fTemp570);
			int iTemp573 = fRec164[0] > fTemp572;
			double fTemp574 = ((iTemp573) ? fRec164[0] : fTemp572);
			int iTemp575 = fRec162[0] > fTemp574;
			double fTemp576 = ((iTemp575) ? fRec162[0] : fTemp574);
			int iTemp577 = fRec160[0] > fTemp576;
			double fTemp578 = ((iTemp577) ? fRec160[0] : fTemp576);
			int iTemp579 = fRec158[0] > fTemp578;
			double fTemp580 = ((iTemp579) ? fRec158[0] : fTemp578);
			int iTemp581 = fRec156[0] > fTemp580;
			double fTemp582 = ((iTemp581) ? fRec156[0] : fTemp580);
			int iTemp583 = fRec154[0] > fTemp582;
			double fTemp584 = ((iTemp583) ? fRec154[0] : fTemp582);
			int iTemp585 = fRec152[0] > fTemp584;
			double fTemp586 = ((iTemp585) ? fRec152[0] : fTemp584);
			int iTemp587 = fRec150[0] > fTemp586;
			double fTemp588 = ((iTemp587) ? fRec150[0] : fTemp586);
			int iTemp589 = fRec148[0] > fTemp588;
			double fTemp590 = ((iTemp589) ? fRec148[0] : fTemp588);
			int iTemp591 = fRec146[0] > fTemp590;
			double fTemp592 = static_cast<double>(std::abs((2e+01 * std::log10((1e-09 + ((iTemp591) ? fRec146[0] : fTemp590)) / (1e-09 + 0.03125 * (fRec208[0] + fRec206[0] + fRec204[0] + fRec202[0] + fRec200[0] + fRec198[0] + fRec196[0] + fRec194[0] + fRec192[0] + fRec190[0] + fRec188[0] + fRec186[0] + fRec184[0] + fRec182[0] + fRec180[0] + fRec178[0] + fRec176[0] + fRec174[0] + fRec172[0] + fRec170[0] + fRec168[0] + fRec166[0] + fRec164[0] + fRec162[0] + fRec160[0] + fRec158[0] + fRec156[0] + fRec154[0] + fRec152[0] + fRec150[0] + fRec146[0] + fRec148[0])))) > fSlow7));
			double fTemp593 = ((fTemp592 > fRec145[1]) ? fSlow13 : fSlow11);
			double fTemp594 = fTemp592 * (1.0 - fTemp593) + fRec145[1] * fTemp593;
			fRec145[0] = ((std::fabs(fTemp594) > 2.2250738585072014e-308) ? fTemp594 : 0.0);
			double fTemp595 = ((fRec145[0] > 0.5) ? ((iTemp591) ? 6e+03 : ((iTemp589) ? 5326.86751846578 : ((iTemp587) ? 4729.252926547629 : ((iTemp585) ? 4198.6839668392995 : ((iTemp583) ? 3727.638873876553 : ((iTemp581) ? 3309.4397396372287 : ((iTemp579) ? 2938.1578422322327 : ((iTemp577) ? 2608.529595652064 : ((iTemp575) ? 2315.8819290059428 : ((iTemp573) ? 2056.0660373706055 : ((iTemp571) ? 1825.398565048354 : ((iTemp569) ? 1620.609387401687 : ((iTemp567) ? 1438.7952509784623 : ((iTemp565) ? 1277.3786146933319 : ((iTemp563) ? 1134.0711085654536 : ((iTemp561) ? 1006.8410919746325 : ((iTemp559) ? 893.884851516048 : ((iTemp557) ? 793.6010301315737 : ((iTemp555) ? 704.5679250048105 : ((iTemp553) ? 625.5233323768265 : ((iTemp551) ? 555.3466535467651 : ((iTemp549) ? 493.043008377822 : ((iTemp547) ? 437.7291310890786 : ((iTemp545) ? 388.620848380777 : ((iTemp543) ? 345.02196237302934 : ((iTemp541) ? 306.3143807537021 : ((iTemp539) ? 271.9493542126425 : ((iTemp537) ? 241.4396969371784 : ((iTemp535) ? 214.3528798804796 : ((iTemp533) ? 190.3048988874873 : ((iTemp531) ? 168.95483074811176 : 1.5e+02))))))))))))))))))))))))))))))) : fRec144[1]);
			fRec144[0] = ((std::fabs(fTemp595) > 2.2250738585072014e-308) ? fTemp595 : 0.0);
			double fTemp596 = fConst8 * std::min<double>(6e+03, std::max<double>(1.5e+02, fRec144[0])) + fConst7 * fRec143[1];
			fRec143[0] = ((std::fabs(fTemp596) > 2.2250738585072014e-308) ? fTemp596 : 0.0);
			double fTemp597 = fRec4[1] * std::cos(fConst6 * fRec143[0]);
			double fTemp598 = fTemp396 + fConst5 * (fTemp401 + fTemp597) - fConst4 * fRec4[2];
			fRec4[0] = ((std::fabs(fTemp598) > 2.2250738585072014e-308) ? fTemp598 : 0.0);
			double fTemp599 = fSlow14 * std::max<double>(0.0, std::min<double>(1.0, fRec145[0])) + fConst236 * fRec210[1];
			fRec210[0] = ((std::fabs(fTemp599) > 2.2250738585072014e-308) ? fTemp599 : 0.0);
			double fTemp600 = ((iSlow6) ? fTemp2 : fConst5 * (0.5 * fRec4[0] - fTemp597 + 0.5 * fRec4[2]) * fRec210[0] + fTemp402 * (1.0 - fRec210[0]));
			double fTemp601 = fTemp1 * fTemp600 - fSlow5 * (fSlow15 * fRec3[2] + fSlow16 * fRec3[1]);
			fRec3[0] = ((std::fabs(fTemp601) > 2.2250738585072014e-308) ? fTemp601 : 0.0);
			double fTemp602 = fSlow5 * (fRec3[2] + fRec3[0] + 2.0 * fRec3[1]) - fSlow4 * (fSlow17 * fRec2[2] + fSlow16 * fRec2[1]);
			fRec2[0] = ((std::fabs(fTemp602) > 2.2250738585072014e-308) ? fTemp602 : 0.0);
			double fTemp603 = fSlow4 * (fRec2[2] + fRec2[0] + 2.0 * fRec2[1]) - fSlow18 * (fSlow19 * fRec1[2] + fSlow16 * fRec1[1]);
			fRec1[0] = ((std::fabs(fTemp603) > 2.2250738585072014e-308) ? fTemp603 : 0.0);
			double fTemp604 = fRec1[2] + fRec1[0] + 2.0 * fRec1[1];
			double fTemp605 = static_cast<double>(iRec0[1]);
			double fTemp606 = fTemp600 * static_cast<double>(-iRec0[1]) - fSlow5 * (fSlow15 * fRec213[2] + fSlow16 * fRec213[1]);
			fRec213[0] = ((std::fabs(fTemp606) > 2.2250738585072014e-308) ? fTemp606 : 0.0);
			double fTemp607 = fSlow5 * (fRec213[2] + fRec213[0] + 2.0 * fRec213[1]) - fSlow4 * (fSlow17 * fRec212[2] + fSlow16 * fRec212[1]);
			fRec212[0] = ((std::fabs(fTemp607) > 2.2250738585072014e-308) ? fTemp607 : 0.0);
			double fTemp608 = fSlow4 * (fRec212[2] + fRec212[0] + 2.0 * fRec212[1]) - fSlow18 * (fSlow19 * fRec211[2] + fSlow16 * fRec211[1]);
			fRec211[0] = ((std::fabs(fTemp608) > 2.2250738585072014e-308) ? fTemp608 : 0.0);
			double fTemp609 = fRec211[2] + fRec211[0] + 2.0 * fRec211[1];
			double fTemp610 = ((iTemp0) ? 0.0 : fSlow20 + fRec215[1]);
			double fTemp611 = fTemp610 - std::floor(fTemp610);
			fRec215[0] = ((std::fabs(fTemp611) > 2.2250738585072014e-308) ? fTemp611 : 0.0);
			int iTemp612 = std::max<int>(0, std::min<int>(static_cast<int>(65536.0 * fRec215[0]), 65535));
			output0[i0] = static_cast<FAUSTFLOAT>(fSlow3 * ((fTemp1 * fTemp604 - fTemp605 * fTemp609) * ftbl0icc_suppressor_f64SIG0[iTemp612] - (fTemp1 * fTemp609 + fTemp604 * fTemp605) * ftbl1icc_suppressor_f64SIG1[iTemp612]));
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
