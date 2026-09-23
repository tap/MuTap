/* ------------------------------------------------------------
name: "dattorro_paper"
Code generated with Faust 2.88.0 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn dattorro_paper_f32 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 1
------------------------------------------------------------ */

#ifndef  __dattorro_paper_f32_H__
#define  __dattorro_paper_f32_H__

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
#define FAUSTCLASS dattorro_paper_f32
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


class dattorro_paper_f32 : public dsp {
	
 private:
	
	FAUSTFLOAT fEntry0;
	int IOTA0;
	float fVec0[8192];
	float fVec1[8192];
	int fSampleRate;
	float fConst0;
	int iConst1;
	float fRec1[2];
	int iConst2;
	float fRec3[2];
	FAUSTFLOAT fEntry1;
	float fVec2[32768];
	int iConst3;
	float fRec4[2];
	float fVec3[32768];
	int iConst4;
	float fRec5[2];
	FAUSTFLOAT fEntry2;
	FAUSTFLOAT fEntry3;
	float fVec4[16384];
	int iConst5;
	float fRec6[2];
	float fVec5[32768];
	int iConst6;
	float fRec7[2];
	FAUSTFLOAT fEntry4;
	FAUSTFLOAT fEntry5;
	float fRec15[3];
	FAUSTFLOAT fEntry6;
	float fVec6[1024];
	int iConst7;
	float fRec14[2];
	float fVec7[1024];
	int iConst8;
	float fRec13[2];
	float fVec8[4096];
	int iConst9;
	float fRec12[2];
	float fVec9[2048];
	int iConst10;
	float fRec16[2];
	float fVec10[32768];
	int iConst11;
	float fRec10[2];
	float fVec11[32768];
	int iConst12;
	float fRec11[2];
	
 public:
	dattorro_paper_f32() {
	}
	
	dattorro_paper_f32(const dattorro_paper_f32&) = default;
	
	virtual ~dattorro_paper_f32() = default;
	
	dattorro_paper_f32& operator=(const dattorro_paper_f32&) = default;
	
	void metadata(Meta* m) { 
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.23.0");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn dattorro_paper_f32 -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 1");
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
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, static_cast<float>(fSampleRate)));
		iConst1 = static_cast<int>(0.030509727f * fConst0 + 0.5f);
		iConst2 = static_cast<int>(0.022579886f * fConst0 + 0.5f);
		iConst3 = static_cast<int>(0.14962535f * fConst0 + 0.5f);
		iConst4 = static_cast<int>(0.14169551f * fConst0 + 0.5f);
		iConst5 = static_cast<int>(0.06048184f * fConst0 + 0.5f);
		iConst6 = static_cast<int>(0.08924431f * fConst0 + 0.5f);
		iConst7 = static_cast<int>(0.004771345f * fConst0 + 0.5f);
		iConst8 = static_cast<int>(0.0035953093f * fConst0 + 0.5f);
		iConst9 = static_cast<int>(0.012734788f * fConst0 + 0.5f);
		iConst10 = static_cast<int>(0.009307483f * fConst0 + 0.5f);
		iConst11 = static_cast<int>(0.10628003f * fConst0 + 0.5f);
		iConst12 = static_cast<int>(0.1249958f * fConst0 + 0.5f);
	}
	
	virtual void instanceResetUserInterface() {
		fEntry0 = static_cast<FAUSTFLOAT>(0.7f);
		fEntry1 = static_cast<FAUSTFLOAT>(0.0005f);
		fEntry2 = static_cast<FAUSTFLOAT>(0.5f);
		fEntry3 = static_cast<FAUSTFLOAT>(0.5f);
		fEntry4 = static_cast<FAUSTFLOAT>(0.625f);
		fEntry5 = static_cast<FAUSTFLOAT>(0.9995f);
		fEntry6 = static_cast<FAUSTFLOAT>(0.75f);
	}
	
	virtual void instanceClear() {
		IOTA0 = 0;
		for (int l0 = 0; l0 < 8192; l0 = faust_wrap_add(l0, 1)) {
			fVec0[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 8192; l1 = faust_wrap_add(l1, 1)) {
			fVec1[l1] = 0.0f;
		}
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			fRec1[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fRec3[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 32768; l4 = faust_wrap_add(l4, 1)) {
			fVec2[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = faust_wrap_add(l5, 1)) {
			fRec4[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 32768; l6 = faust_wrap_add(l6, 1)) {
			fVec3[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = faust_wrap_add(l7, 1)) {
			fRec5[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 16384; l8 = faust_wrap_add(l8, 1)) {
			fVec4[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2; l9 = faust_wrap_add(l9, 1)) {
			fRec6[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 32768; l10 = faust_wrap_add(l10, 1)) {
			fVec5[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = faust_wrap_add(l11, 1)) {
			fRec7[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 3; l12 = faust_wrap_add(l12, 1)) {
			fRec15[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 1024; l13 = faust_wrap_add(l13, 1)) {
			fVec6[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 2; l14 = faust_wrap_add(l14, 1)) {
			fRec14[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 1024; l15 = faust_wrap_add(l15, 1)) {
			fVec7[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 2; l16 = faust_wrap_add(l16, 1)) {
			fRec13[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 4096; l17 = faust_wrap_add(l17, 1)) {
			fVec8[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 2; l18 = faust_wrap_add(l18, 1)) {
			fRec12[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2048; l19 = faust_wrap_add(l19, 1)) {
			fVec9[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = faust_wrap_add(l20, 1)) {
			fRec16[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 32768; l21 = faust_wrap_add(l21, 1)) {
			fVec10[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 2; l22 = faust_wrap_add(l22, 1)) {
			fRec10[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 32768; l23 = faust_wrap_add(l23, 1)) {
			fVec11[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 2; l24 = faust_wrap_add(l24, 1)) {
			fRec11[l24] = 0.0f;
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
	
	virtual dattorro_paper_f32* clone() {
		return new dattorro_paper_f32(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("dattorro_paper");
		ui_interface->addNumEntry("bw", &fEntry5, FAUSTFLOAT(0.9995f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.0001f));
		ui_interface->addNumEntry("d_diff1", &fEntry0, FAUSTFLOAT(0.7f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addNumEntry("d_diff2", &fEntry3, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addNumEntry("damping", &fEntry1, FAUSTFLOAT(0.0005f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.0001f));
		ui_interface->addNumEntry("decay", &fEntry2, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.0f), FAUSTFLOAT(0.9999f), FAUSTFLOAT(0.0001f));
		ui_interface->addNumEntry("i_diff1", &fEntry6, FAUSTFLOAT(0.75f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->addNumEntry("i_diff2", &fEntry4, FAUSTFLOAT(0.625f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.001f));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = static_cast<float>(fEntry0);
		float fSlow1 = static_cast<float>(fEntry1);
		float fSlow2 = 1.0f - fSlow1;
		float fSlow3 = static_cast<float>(fEntry2);
		float fSlow4 = static_cast<float>(fEntry3);
		float fSlow5 = static_cast<float>(fEntry4);
		float fSlow6 = static_cast<float>(fEntry5);
		float fSlow7 = 1.0f - fSlow6;
		float fSlow8 = static_cast<float>(fEntry6);
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			float fTemp0 = fSlow0 * fRec3[1] + fRec10[1];
			fVec0[IOTA0 & 8191] = fTemp0;
			float fTemp1 = fSlow0 * fTemp0;
			float fRec0 = ((std::fabs(-fTemp1) > 1.1754944e-38f) ? -fTemp1 : 0.0f);
			float fTemp2 = fSlow0 * fRec1[1] + fRec11[1];
			fVec1[IOTA0 & 8191] = fTemp2;
			float fTemp3 = fVec1[(faust_wrap_sub(IOTA0, iConst1)) & 8191];
			fRec1[0] = ((std::fabs(fTemp3) > 1.1754944e-38f) ? fTemp3 : 0.0f);
			float fTemp4 = fSlow0 * fTemp2;
			float fRec2 = ((std::fabs(-fTemp4) > 1.1754944e-38f) ? -fTemp4 : 0.0f);
			float fTemp5 = fVec0[(faust_wrap_sub(IOTA0, iConst2)) & 8191];
			fRec3[0] = ((std::fabs(fTemp5) > 1.1754944e-38f) ? fTemp5 : 0.0f);
			fVec2[IOTA0 & 32767] = fRec3[1] + fRec0;
			float fTemp6 = fSlow1 * fRec4[1] + fSlow2 * fVec2[(faust_wrap_sub(IOTA0, iConst3)) & 32767];
			fRec4[0] = ((std::fabs(fTemp6) > 1.1754944e-38f) ? fTemp6 : 0.0f);
			fVec3[IOTA0 & 32767] = fRec1[1] + fRec2;
			float fTemp7 = fSlow1 * fRec5[1] + fSlow2 * fVec3[(faust_wrap_sub(IOTA0, iConst4)) & 32767];
			fRec5[0] = ((std::fabs(fTemp7) > 1.1754944e-38f) ? fTemp7 : 0.0f);
			float fTemp8 = fSlow3 * fRec4[0] - fSlow4 * fRec6[1];
			fVec4[IOTA0 & 16383] = fTemp8;
			float fTemp9 = fVec4[(faust_wrap_sub(IOTA0, iConst5)) & 16383];
			fRec6[0] = ((std::fabs(fTemp9) > 1.1754944e-38f) ? fTemp9 : 0.0f);
			float fTemp10 = fSlow3 * fRec5[0] - fSlow4 * fRec7[1];
			fVec5[IOTA0 & 32767] = fTemp10;
			float fTemp11 = fVec5[(faust_wrap_sub(IOTA0, iConst6)) & 32767];
			fRec7[0] = ((std::fabs(fTemp11) > 1.1754944e-38f) ? fTemp11 : 0.0f);
			float fTemp12 = fSlow4 * fTemp10;
			float fRec8 = ((std::fabs(fTemp12) > 1.1754944e-38f) ? fTemp12 : 0.0f);
			float fTemp13 = fSlow4 * fTemp8;
			float fRec9 = ((std::fabs(fTemp13) > 1.1754944e-38f) ? fTemp13 : 0.0f);
			float fTemp14 = fSlow6 * static_cast<float>(input0[i0]) + fSlow7 * fRec15[2];
			fRec15[0] = ((std::fabs(fTemp14) > 1.1754944e-38f) ? fTemp14 : 0.0f);
			float fTemp15 = fRec15[0] - fSlow8 * fRec14[1];
			fVec6[IOTA0 & 1023] = fTemp15;
			float fTemp16 = fVec6[(faust_wrap_sub(IOTA0, iConst7)) & 1023];
			fRec14[0] = ((std::fabs(fTemp16) > 1.1754944e-38f) ? fTemp16 : 0.0f);
			float fTemp17 = fRec14[1] - fSlow8 * (fRec13[1] - fTemp15);
			fVec7[IOTA0 & 1023] = fTemp17;
			float fTemp18 = fVec7[(faust_wrap_sub(IOTA0, iConst8)) & 1023];
			fRec13[0] = ((std::fabs(fTemp18) > 1.1754944e-38f) ? fTemp18 : 0.0f);
			float fTemp19 = fRec13[1] + fSlow8 * fTemp17 - fSlow5 * fRec12[1];
			fVec8[IOTA0 & 4095] = fTemp19;
			float fTemp20 = fVec8[(faust_wrap_sub(IOTA0, iConst9)) & 4095];
			fRec12[0] = ((std::fabs(fTemp20) > 1.1754944e-38f) ? fTemp20 : 0.0f);
			float fTemp21 = fRec12[1] - fSlow5 * (fRec16[1] - fTemp19);
			fVec9[IOTA0 & 2047] = fTemp21;
			float fTemp22 = fVec9[(faust_wrap_sub(IOTA0, iConst10)) & 2047];
			fRec16[0] = ((std::fabs(fTemp22) > 1.1754944e-38f) ? fTemp22 : 0.0f);
			float fTemp23 = fSlow5 * fTemp21;
			fVec10[IOTA0 & 32767] = fRec7[1] + fRec8;
			float fTemp24 = fTemp23 + fSlow3 * fVec10[(faust_wrap_sub(IOTA0, iConst11)) & 32767] + fRec16[1];
			fRec10[0] = ((std::fabs(fTemp24) > 1.1754944e-38f) ? fTemp24 : 0.0f);
			fVec11[IOTA0 & 32767] = fRec6[1] + fRec9;
			float fTemp25 = fRec16[1] + fTemp23 + fSlow3 * fVec11[(faust_wrap_sub(IOTA0, iConst12)) & 32767];
			fRec11[0] = ((std::fabs(fTemp25) > 1.1754944e-38f) ? fTemp25 : 0.0f);
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
