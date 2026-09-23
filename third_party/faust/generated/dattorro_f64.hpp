/* ------------------------------------------------------------
name: "dattorro"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn dattorro_f64 -es 1 -mcd 16 -mdd 1024 -mdy 33 -double -ftz 1
------------------------------------------------------------ */

#ifndef  __dattorro_f64_H__
#define  __dattorro_f64_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

#include <algorithm>
#include <cmath>
#include <cstdint>
#ifndef FAUST_INT_WRAP
#define FAUST_INT_WRAP
inline int faust_wrap_add(int a, int b) { return int((unsigned int)a + (unsigned int)b); }
inline int faust_wrap_sub(int a, int b) { return int((unsigned int)a - (unsigned int)b); }
inline int faust_wrap_mul(int a, int b) { return int((unsigned int)a * (unsigned int)b); }
#endif


namespace mutap_faust {

#ifndef FAUSTCLASS 
#define FAUSTCLASS dattorro_f64
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


class dattorro_f64 : public dsp {
	
 private:
	
	FAUSTFLOAT fEntry0;
	int IOTA0;
	double fVec0[1024];
	double fRec0[2];
	double fVec1[1024];
	double fRec2[2];
	FAUSTFLOAT fEntry1;
	double fVec2[8192];
	double fRec4[2];
	double fVec3[8192];
	double fRec5[2];
	FAUSTFLOAT fEntry2;
	FAUSTFLOAT fEntry3;
	double fVec4[4096];
	double fVec5[2048];
	double fRec8[2];
	double fRec9[2];
	FAUSTFLOAT fEntry4;
	FAUSTFLOAT fEntry5;
	double fRec15[3];
	FAUSTFLOAT fEntry6;
	double fVec6[256];
	double fRec14[2];
	double fVec7[128];
	double fRec13[2];
	double fVec8[512];
	double fRec12[2];
	double fVec9[512];
	double fRec16[2];
	double fVec10[4096];
	double fRec10[2];
	double fVec11[2048];
	double fRec11[2];
	int fSampleRate;
	
 public:
	dattorro_f64() {
	}
	
	dattorro_f64(const dattorro_f64&) = default;
	
	virtual ~dattorro_f64() = default;
	
	dattorro_f64& operator=(const dattorro_f64&) = default;
	
	void metadata(Meta* m) { 
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn dattorro_f64 -es 1 -mcd 16 -mdd 1024 -mdy 33 -double -ftz 1");
		m->declare("filename", "dattorro.dsp");
		m->declare("name", "dattorro");
		m->declare("reverbs.lib/dattorro_rev:author", "Jakob Zerbian");
		m->declare("reverbs.lib/dattorro_rev:licence", "LicenseRef-STK-4.3");
		m->declare("reverbs.lib/name", "Faust Reverb Library");
		m->declare("reverbs.lib/version", "1.5.1");
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
		for (int l0 = 0; l0 < 1024; l0 = faust_wrap_add(l0, 1)) {
			fVec0[l0] = 0.0;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			fRec0[l1] = 0.0;
		}
		for (int l2 = 0; l2 < 1024; l2 = faust_wrap_add(l2, 1)) {
			fVec1[l2] = 0.0;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fRec2[l3] = 0.0;
		}
		for (int l4 = 0; l4 < 8192; l4 = faust_wrap_add(l4, 1)) {
			fVec2[l4] = 0.0;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec4[l5] = 0.0;
		}
		for (int l6 = 0; l6 < 8192; l6 = faust_wrap_add(l6, 1)) {
			fVec3[l6] = 0.0;
		}
		for (int l7 = 0; l7 < 2; l7 = faust_wrap_add(l7, 1)) {
			fRec5[l7] = 0.0;
		}
		for (int l8 = 0; l8 < 4096; l8 = faust_wrap_add(l8, 1)) {
			fVec4[l8] = 0.0;
		}
		for (int l9 = 0; l9 < 2048; l9 = faust_wrap_add(l9, 1)) {
			fVec5[l9] = 0.0;
		}
		for (int l10 = 0; l10 < 2; l10 = faust_wrap_add(l10, 1)) {
			fRec8[l10] = 0.0;
		}
		for (int l11 = 0; l11 < 2; l11 = faust_wrap_add(l11, 1)) {
			fRec9[l11] = 0.0;
		}
		for (int l12 = 0; l12 < 3; l12 = faust_wrap_add(l12, 1)) {
			fRec15[l12] = 0.0;
		}
		for (int l13 = 0; l13 < 256; l13 = faust_wrap_add(l13, 1)) {
			fVec6[l13] = 0.0;
		}
		for (int l14 = 0; l14 < 2; l14 = faust_wrap_add(l14, 1)) {
			fRec14[l14] = 0.0;
		}
		for (int l15 = 0; l15 < 128; l15 = faust_wrap_add(l15, 1)) {
			fVec7[l15] = 0.0;
		}
		for (int l16 = 0; l16 < 2; l16 = faust_wrap_add(l16, 1)) {
			fRec13[l16] = 0.0;
		}
		for (int l17 = 0; l17 < 512; l17 = faust_wrap_add(l17, 1)) {
			fVec8[l17] = 0.0;
		}
		for (int l18 = 0; l18 < 2; l18 = faust_wrap_add(l18, 1)) {
			fRec12[l18] = 0.0;
		}
		for (int l19 = 0; l19 < 512; l19 = faust_wrap_add(l19, 1)) {
			fVec9[l19] = 0.0;
		}
		for (int l20 = 0; l20 < 2; l20 = faust_wrap_add(l20, 1)) {
			fRec16[l20] = 0.0;
		}
		for (int l21 = 0; l21 < 4096; l21 = faust_wrap_add(l21, 1)) {
			fVec10[l21] = 0.0;
		}
		for (int l22 = 0; l22 < 2; l22 = faust_wrap_add(l22, 1)) {
			fRec10[l22] = 0.0;
		}
		for (int l23 = 0; l23 < 2048; l23 = faust_wrap_add(l23, 1)) {
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
	
	virtual dattorro_f64* clone() {
		return new dattorro_f64(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("dattorro");
		ui_interface->addNumEntry("bw", &fEntry5, FAUSTFLOAT(0.9995), FAUSTFLOAT(0.0), FAUSTFLOAT(1.0), FAUSTFLOAT(0.0001));
		ui_interface->addNumEntry("d_diff1", &fEntry0, FAUSTFLOAT(0.7), FAUSTFLOAT(0.0), FAUSTFLOAT(1.0), FAUSTFLOAT(0.001));
		ui_interface->addNumEntry("d_diff2", &fEntry2, FAUSTFLOAT(0.5), FAUSTFLOAT(0.0), FAUSTFLOAT(1.0), FAUSTFLOAT(0.001));
		ui_interface->addNumEntry("damping", &fEntry1, FAUSTFLOAT(0.0005), FAUSTFLOAT(0.0), FAUSTFLOAT(1.0), FAUSTFLOAT(0.0001));
		ui_interface->addNumEntry("decay", &fEntry3, FAUSTFLOAT(0.5), FAUSTFLOAT(0.0), FAUSTFLOAT(0.9999), FAUSTFLOAT(0.0001));
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
			double fTemp0 = fSlow0 * fRec0[1] + fRec11[1];
			fVec0[IOTA0 & 1023] = fTemp0;
			double fTemp1 = fVec0[(faust_wrap_sub(IOTA0, 908)) & 1023];
			fRec0[0] = ((std::fabs(fTemp1) > 2.2250738585072014e-308) ? fTemp1 : 0.0);
			double fTemp2 = fSlow0 * fTemp0;
			double fRec1 = ((std::fabs(-fTemp2) > 2.2250738585072014e-308) ? -fTemp2 : 0.0);
			double fTemp3 = fSlow0 * fRec2[1] + fRec10[1];
			fVec1[IOTA0 & 1023] = fTemp3;
			double fTemp4 = fVec1[(faust_wrap_sub(IOTA0, 672)) & 1023];
			fRec2[0] = ((std::fabs(fTemp4) > 2.2250738585072014e-308) ? fTemp4 : 0.0);
			double fTemp5 = fSlow0 * fTemp3;
			double fRec3 = ((std::fabs(-fTemp5) > 2.2250738585072014e-308) ? -fTemp5 : 0.0);
			fVec2[IOTA0 & 8191] = fRec2[1] + fRec3;
			double fTemp6 = fSlow1 * fRec4[1] + fSlow2 * fVec2[(faust_wrap_sub(IOTA0, 4453)) & 8191];
			fRec4[0] = ((std::fabs(fTemp6) > 2.2250738585072014e-308) ? fTemp6 : 0.0);
			fVec3[IOTA0 & 8191] = fRec0[1] + fRec1;
			double fTemp7 = fSlow1 * fRec5[1] + fSlow2 * fVec3[(faust_wrap_sub(IOTA0, 4217)) & 8191];
			fRec5[0] = ((std::fabs(fTemp7) > 2.2250738585072014e-308) ? fTemp7 : 0.0);
			double fTemp8 = fSlow4 * fRec5[0] - fSlow3 * fRec9[1];
			fVec4[IOTA0 & 4095] = fTemp8;
			double fTemp9 = fSlow3 * fTemp8;
			double fRec6 = ((std::fabs(fTemp9) > 2.2250738585072014e-308) ? fTemp9 : 0.0);
			double fTemp10 = fSlow4 * fRec4[0] - fSlow3 * fRec8[1];
			fVec5[IOTA0 & 2047] = fTemp10;
			double fTemp11 = fSlow3 * fTemp10;
			double fRec7 = ((std::fabs(fTemp11) > 2.2250738585072014e-308) ? fTemp11 : 0.0);
			double fTemp12 = fVec5[(faust_wrap_sub(IOTA0, 1800)) & 2047];
			fRec8[0] = ((std::fabs(fTemp12) > 2.2250738585072014e-308) ? fTemp12 : 0.0);
			double fTemp13 = fVec4[(faust_wrap_sub(IOTA0, 2656)) & 4095];
			fRec9[0] = ((std::fabs(fTemp13) > 2.2250738585072014e-308) ? fTemp13 : 0.0);
			double fTemp14 = fSlow6 * static_cast<double>(input0[i0]) + fSlow7 * fRec15[2];
			fRec15[0] = ((std::fabs(fTemp14) > 2.2250738585072014e-308) ? fTemp14 : 0.0);
			double fTemp15 = fRec15[0] - fSlow8 * fRec14[1];
			fVec6[IOTA0 & 255] = fTemp15;
			double fTemp16 = fVec6[(faust_wrap_sub(IOTA0, 142)) & 255];
			fRec14[0] = ((std::fabs(fTemp16) > 2.2250738585072014e-308) ? fTemp16 : 0.0);
			double fTemp17 = fRec14[1] - fSlow8 * (fRec13[1] - fTemp15);
			fVec7[IOTA0 & 127] = fTemp17;
			double fTemp18 = fVec7[(faust_wrap_sub(IOTA0, 107)) & 127];
			fRec13[0] = ((std::fabs(fTemp18) > 2.2250738585072014e-308) ? fTemp18 : 0.0);
			double fTemp19 = fRec13[1] + fSlow8 * fTemp17 - fSlow5 * fRec12[1];
			fVec8[IOTA0 & 511] = fTemp19;
			double fTemp20 = fVec8[(faust_wrap_sub(IOTA0, 379)) & 511];
			fRec12[0] = ((std::fabs(fTemp20) > 2.2250738585072014e-308) ? fTemp20 : 0.0);
			double fTemp21 = fRec12[1] - fSlow5 * (fRec16[1] - fTemp19);
			fVec9[IOTA0 & 511] = fTemp21;
			double fTemp22 = fVec9[(faust_wrap_sub(IOTA0, 277)) & 511];
			fRec16[0] = ((std::fabs(fTemp22) > 2.2250738585072014e-308) ? fTemp22 : 0.0);
			double fTemp23 = fSlow5 * fTemp21;
			fVec10[IOTA0 & 4095] = fRec9[1] + fRec6;
			double fTemp24 = fTemp23 + fSlow4 * fVec10[(faust_wrap_sub(IOTA0, 2656)) & 4095] + fRec16[1];
			fRec10[0] = ((std::fabs(fTemp24) > 2.2250738585072014e-308) ? fTemp24 : 0.0);
			fVec11[IOTA0 & 2047] = fRec8[1] + fRec7;
			double fTemp25 = fRec16[1] + fTemp23 + fSlow4 * fVec11[(faust_wrap_sub(IOTA0, 1800)) & 2047];
			fRec11[0] = ((std::fabs(fTemp25) > 2.2250738585072014e-308) ? fTemp25 : 0.0);
			output0[i0] = static_cast<FAUSTFLOAT>(fRec10[0]);
			output1[i0] = static_cast<FAUSTFLOAT>(fRec11[0]);
			IOTA0 = faust_wrap_add(IOTA0, 1);
			fRec0[1] = fRec0[0];
			fRec2[1] = fRec2[0];
			fRec4[1] = fRec4[0];
			fRec5[1] = fRec5[0];
			fRec8[1] = fRec8[0];
			fRec9[1] = fRec9[0];
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
