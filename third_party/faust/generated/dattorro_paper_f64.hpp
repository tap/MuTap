/* ------------------------------------------------------------
name: "dattorro_paper"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn dattorro_paper_f64 -es 1 -mcd 16 -mdd 1024 -mdy 33 -double -ftz 1
------------------------------------------------------------ */

#ifndef  __dattorro_paper_f64_H__
#define  __dattorro_paper_f64_H__

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
#define FAUSTCLASS dattorro_paper_f64
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


class dattorro_paper_f64 : public dsp {
	
 private:
	
	FAUSTFLOAT fEntry0;
	int IOTA0;
	double fVec0[8192];
	double fVec1[8192];
	int fSampleRate;
	double fConst0;
	int iConst1;
	double fRec1[2];
	int iConst2;
	double fRec3[2];
	FAUSTFLOAT fEntry1;
	double fVec2[32768];
	int iConst3;
	double fRec4[2];
	double fVec3[32768];
	int iConst4;
	double fRec5[2];
	FAUSTFLOAT fEntry2;
	FAUSTFLOAT fEntry3;
	double fVec4[16384];
	int iConst5;
	double fRec6[2];
	double fVec5[32768];
	int iConst6;
	double fRec7[2];
	FAUSTFLOAT fEntry4;
	FAUSTFLOAT fEntry5;
	double fRec15[3];
	FAUSTFLOAT fEntry6;
	double fVec6[1024];
	int iConst7;
	double fRec14[2];
	double fVec7[1024];
	int iConst8;
	double fRec13[2];
	double fVec8[4096];
	int iConst9;
	double fRec12[2];
	double fVec9[2048];
	int iConst10;
	double fRec16[2];
	double fVec10[32768];
	int iConst11;
	double fRec10[2];
	double fVec11[32768];
	int iConst12;
	double fRec11[2];
	
 public:
	dattorro_paper_f64() {
	}
	
	dattorro_paper_f64(const dattorro_paper_f64&) = default;
	
	virtual ~dattorro_paper_f64() = default;
	
	dattorro_paper_f64& operator=(const dattorro_paper_f64&) = default;
	
	void metadata(Meta* m) { 
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn dattorro_paper_f64 -es 1 -mcd 16 -mdd 1024 -mdy 33 -double -ftz 1");
		m->declare("filename", "dattorro_paper.dsp");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.0");
		m->declare("name", "dattorro_paper");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.0");
		m->declare("routes.lib/name", "Faust Signal Routing Library");
		m->declare("routes.lib/version", "1.4.0");
		m->declare("signals.lib/name", "Faust Routing Library");
		m->declare("signals.lib/version", "1.7.0");
	}

	virtual int getNumInputs() {
		return 1;
	}
	virtual int getNumOutputs() {
		return 2;
	}
	
	static void classInit(int sample_rate) {
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<double>(1.92e+05, std::max<double>(1.0, static_cast<double>(fSampleRate)));
		iConst1 = static_cast<int>(0.030509727495715868 * fConst0 + 0.5);
		iConst2 = static_cast<int>(0.022579886428547427 * fConst0 + 0.5);
		iConst3 = static_cast<int>(0.14962534861059776 * fConst0 + 0.5);
		iConst4 = static_cast<int>(0.14169550754342933 * fConst0 + 0.5);
		iConst5 = static_cast<int>(0.060481838647894894 * fConst0 + 0.5);
		iConst6 = static_cast<int>(0.08924431302711602 * fConst0 + 0.5);
		iConst7 = static_cast<int>(0.004771345048889486 * fConst0 + 0.5);
		iConst8 = static_cast<int>(0.0035953092974026412 * fConst0 + 0.5);
		iConst9 = static_cast<int>(0.01273478713752898 * fConst0 + 0.5);
		iConst10 = static_cast<int>(0.009307482947481604 * fConst0 + 0.5);
		iConst11 = static_cast<int>(0.10628003091293975 * fConst0 + 0.5);
		iConst12 = static_cast<int>(0.12499579987231611 * fConst0 + 0.5);
	}
	
	virtual void instanceResetUserInterface() {
		fEntry0 = static_cast<FAUSTFLOAT>(0.7);
		fEntry1 = static_cast<FAUSTFLOAT>(0.0005);
		fEntry2 = static_cast<FAUSTFLOAT>(0.5);
		fEntry3 = static_cast<FAUSTFLOAT>(0.5);
		fEntry4 = static_cast<FAUSTFLOAT>(0.625);
		fEntry5 = static_cast<FAUSTFLOAT>(0.9995);
		fEntry6 = static_cast<FAUSTFLOAT>(0.75);
	}
	
	virtual void instanceClear() {
		IOTA0 = 0;
		for (int l0 = 0; l0 < 8192; l0 = faust_wrap_add(l0, 1)) {
			fVec0[l0] = 0.0;
		}
		for (int l1 = 0; l1 < 8192; l1 = faust_wrap_add(l1, 1)) {
			fVec1[l1] = 0.0;
		}
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			fRec1[l2] = 0.0;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fRec3[l3] = 0.0;
		}
		for (int l4 = 0; l4 < 32768; l4 = faust_wrap_add(l4, 1)) {
			fVec2[l4] = 0.0;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec4[l5] = 0.0;
		}
		for (int l6 = 0; l6 < 32768; l6 = faust_wrap_add(l6, 1)) {
			fVec3[l6] = 0.0;
		}
		for (int l7 = 0; l7 < 2; l7 = faust_wrap_add(l7, 1)) {
			fRec5[l7] = 0.0;
		}
		for (int l8 = 0; l8 < 16384; l8 = faust_wrap_add(l8, 1)) {
			fVec4[l8] = 0.0;
		}
		for (int l9 = 0; l9 < 2; l9 = faust_wrap_add(l9, 1)) {
			fRec6[l9] = 0.0;
		}
		for (int l10 = 0; l10 < 32768; l10 = faust_wrap_add(l10, 1)) {
			fVec5[l10] = 0.0;
		}
		for (int l11 = 0; l11 < 2; l11 = faust_wrap_add(l11, 1)) {
			fRec7[l11] = 0.0;
		}
		for (int l12 = 0; l12 < 3; l12 = faust_wrap_add(l12, 1)) {
			fRec15[l12] = 0.0;
		}
		for (int l13 = 0; l13 < 1024; l13 = faust_wrap_add(l13, 1)) {
			fVec6[l13] = 0.0;
		}
		for (int l14 = 0; l14 < 2; l14 = faust_wrap_add(l14, 1)) {
			fRec14[l14] = 0.0;
		}
		for (int l15 = 0; l15 < 1024; l15 = faust_wrap_add(l15, 1)) {
			fVec7[l15] = 0.0;
		}
		for (int l16 = 0; l16 < 2; l16 = faust_wrap_add(l16, 1)) {
			fRec13[l16] = 0.0;
		}
		for (int l17 = 0; l17 < 4096; l17 = faust_wrap_add(l17, 1)) {
			fVec8[l17] = 0.0;
		}
		for (int l18 = 0; l18 < 2; l18 = faust_wrap_add(l18, 1)) {
			fRec12[l18] = 0.0;
		}
		for (int l19 = 0; l19 < 2048; l19 = faust_wrap_add(l19, 1)) {
			fVec9[l19] = 0.0;
		}
		for (int l20 = 0; l20 < 2; l20 = faust_wrap_add(l20, 1)) {
			fRec16[l20] = 0.0;
		}
		for (int l21 = 0; l21 < 32768; l21 = faust_wrap_add(l21, 1)) {
			fVec10[l21] = 0.0;
		}
		for (int l22 = 0; l22 < 2; l22 = faust_wrap_add(l22, 1)) {
			fRec10[l22] = 0.0;
		}
		for (int l23 = 0; l23 < 32768; l23 = faust_wrap_add(l23, 1)) {
			fVec11[l23] = 0.0;
		}
		for (int l24 = 0; l24 < 2; l24 = faust_wrap_add(l24, 1)) {
			fRec11[l24] = 0.0;
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
	
	virtual dattorro_paper_f64* clone() {
		return new dattorro_paper_f64(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("dattorro_paper");
		ui_interface->addNumEntry("bw", &fEntry5, FAUSTFLOAT(0.9995), FAUSTFLOAT(0.0), FAUSTFLOAT(1.0), FAUSTFLOAT(0.0001));
		ui_interface->addNumEntry("d_diff1", &fEntry0, FAUSTFLOAT(0.7), FAUSTFLOAT(0.0), FAUSTFLOAT(1.0), FAUSTFLOAT(0.001));
		ui_interface->addNumEntry("d_diff2", &fEntry3, FAUSTFLOAT(0.5), FAUSTFLOAT(0.0), FAUSTFLOAT(1.0), FAUSTFLOAT(0.001));
		ui_interface->addNumEntry("damping", &fEntry1, FAUSTFLOAT(0.0005), FAUSTFLOAT(0.0), FAUSTFLOAT(1.0), FAUSTFLOAT(0.0001));
		ui_interface->addNumEntry("decay", &fEntry2, FAUSTFLOAT(0.5), FAUSTFLOAT(0.0), FAUSTFLOAT(0.9999), FAUSTFLOAT(0.0001));
		ui_interface->addNumEntry("i_diff1", &fEntry6, FAUSTFLOAT(0.75), FAUSTFLOAT(0.0), FAUSTFLOAT(1.0), FAUSTFLOAT(0.001));
		ui_interface->addNumEntry("i_diff2", &fEntry4, FAUSTFLOAT(0.625), FAUSTFLOAT(0.0), FAUSTFLOAT(1.0), FAUSTFLOAT(0.001));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		double fSlow0 = static_cast<double>(fEntry0);
		double fSlow1 = static_cast<double>(fEntry1);
		double fSlow2 = 1.0 - fSlow1;
		double fSlow3 = static_cast<double>(fEntry2);
		double fSlow4 = static_cast<double>(fEntry3);
		double fSlow5 = static_cast<double>(fEntry4);
		double fSlow6 = static_cast<double>(fEntry5);
		double fSlow7 = 1.0 - fSlow6;
		double fSlow8 = static_cast<double>(fEntry6);
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			double fTemp0 = fSlow0 * fRec3[1] + fRec10[1];
			fVec0[IOTA0 & 8191] = fTemp0;
			double fTemp1 = fSlow0 * fTemp0;
			double fRec0 = ((std::fabs(-fTemp1) > 2.2250738585072014e-308) ? -fTemp1 : 0.0);
			double fTemp2 = fSlow0 * fRec1[1] + fRec11[1];
			fVec1[IOTA0 & 8191] = fTemp2;
			double fTemp3 = fVec1[(faust_wrap_sub(IOTA0, iConst1)) & 8191];
			fRec1[0] = ((std::fabs(fTemp3) > 2.2250738585072014e-308) ? fTemp3 : 0.0);
			double fTemp4 = fSlow0 * fTemp2;
			double fRec2 = ((std::fabs(-fTemp4) > 2.2250738585072014e-308) ? -fTemp4 : 0.0);
			double fTemp5 = fVec0[(faust_wrap_sub(IOTA0, iConst2)) & 8191];
			fRec3[0] = ((std::fabs(fTemp5) > 2.2250738585072014e-308) ? fTemp5 : 0.0);
			fVec2[IOTA0 & 32767] = fRec3[1] + fRec0;
			double fTemp6 = fSlow1 * fRec4[1] + fSlow2 * fVec2[(faust_wrap_sub(IOTA0, iConst3)) & 32767];
			fRec4[0] = ((std::fabs(fTemp6) > 2.2250738585072014e-308) ? fTemp6 : 0.0);
			fVec3[IOTA0 & 32767] = fRec1[1] + fRec2;
			double fTemp7 = fSlow1 * fRec5[1] + fSlow2 * fVec3[(faust_wrap_sub(IOTA0, iConst4)) & 32767];
			fRec5[0] = ((std::fabs(fTemp7) > 2.2250738585072014e-308) ? fTemp7 : 0.0);
			double fTemp8 = fSlow3 * fRec4[0] - fSlow4 * fRec6[1];
			fVec4[IOTA0 & 16383] = fTemp8;
			double fTemp9 = fVec4[(faust_wrap_sub(IOTA0, iConst5)) & 16383];
			fRec6[0] = ((std::fabs(fTemp9) > 2.2250738585072014e-308) ? fTemp9 : 0.0);
			double fTemp10 = fSlow3 * fRec5[0] - fSlow4 * fRec7[1];
			fVec5[IOTA0 & 32767] = fTemp10;
			double fTemp11 = fVec5[(faust_wrap_sub(IOTA0, iConst6)) & 32767];
			fRec7[0] = ((std::fabs(fTemp11) > 2.2250738585072014e-308) ? fTemp11 : 0.0);
			double fTemp12 = fSlow4 * fTemp10;
			double fRec8 = ((std::fabs(fTemp12) > 2.2250738585072014e-308) ? fTemp12 : 0.0);
			double fTemp13 = fSlow4 * fTemp8;
			double fRec9 = ((std::fabs(fTemp13) > 2.2250738585072014e-308) ? fTemp13 : 0.0);
			double fTemp14 = fSlow6 * static_cast<double>(input0[i0]) + fSlow7 * fRec15[2];
			fRec15[0] = ((std::fabs(fTemp14) > 2.2250738585072014e-308) ? fTemp14 : 0.0);
			double fTemp15 = fRec15[0] - fSlow8 * fRec14[1];
			fVec6[IOTA0 & 1023] = fTemp15;
			double fTemp16 = fVec6[(faust_wrap_sub(IOTA0, iConst7)) & 1023];
			fRec14[0] = ((std::fabs(fTemp16) > 2.2250738585072014e-308) ? fTemp16 : 0.0);
			double fTemp17 = fRec14[1] - fSlow8 * (fRec13[1] - fTemp15);
			fVec7[IOTA0 & 1023] = fTemp17;
			double fTemp18 = fVec7[(faust_wrap_sub(IOTA0, iConst8)) & 1023];
			fRec13[0] = ((std::fabs(fTemp18) > 2.2250738585072014e-308) ? fTemp18 : 0.0);
			double fTemp19 = fRec13[1] + fSlow8 * fTemp17 - fSlow5 * fRec12[1];
			fVec8[IOTA0 & 4095] = fTemp19;
			double fTemp20 = fVec8[(faust_wrap_sub(IOTA0, iConst9)) & 4095];
			fRec12[0] = ((std::fabs(fTemp20) > 2.2250738585072014e-308) ? fTemp20 : 0.0);
			double fTemp21 = fRec12[1] - fSlow5 * (fRec16[1] - fTemp19);
			fVec9[IOTA0 & 2047] = fTemp21;
			double fTemp22 = fVec9[(faust_wrap_sub(IOTA0, iConst10)) & 2047];
			fRec16[0] = ((std::fabs(fTemp22) > 2.2250738585072014e-308) ? fTemp22 : 0.0);
			double fTemp23 = fSlow5 * fTemp21;
			fVec10[IOTA0 & 32767] = fRec7[1] + fRec8;
			double fTemp24 = fTemp23 + fSlow3 * fVec10[(faust_wrap_sub(IOTA0, iConst11)) & 32767] + fRec16[1];
			fRec10[0] = ((std::fabs(fTemp24) > 2.2250738585072014e-308) ? fTemp24 : 0.0);
			fVec11[IOTA0 & 32767] = fRec6[1] + fRec9;
			double fTemp25 = fRec16[1] + fTemp23 + fSlow3 * fVec11[(faust_wrap_sub(IOTA0, iConst12)) & 32767];
			fRec11[0] = ((std::fabs(fTemp25) > 2.2250738585072014e-308) ? fTemp25 : 0.0);
			output0[i0] = static_cast<FAUSTFLOAT>(fRec10[0]);
			output1[i0] = static_cast<FAUSTFLOAT>(fRec11[0]);
			IOTA0 = faust_wrap_add(IOTA0, 1);
			fRec1[1] = fRec1[0];
			fRec3[1] = fRec3[0];
			fRec4[1] = fRec4[0];
			fRec5[1] = fRec5[0];
			fRec6[1] = fRec6[0];
			fRec7[1] = fRec7[0];
			fRec15[2] = fRec15[1];
			fRec15[1] = fRec15[0];
			fRec14[1] = fRec14[0];
			fRec13[1] = fRec13[0];
			fRec12[1] = fRec12[0];
			fRec16[1] = fRec16[0];
			fRec10[1] = fRec10[0];
			fRec11[1] = fRec11[0];
		}
	}

};

} // namespace mutap_faust

#endif
