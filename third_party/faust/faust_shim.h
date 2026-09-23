/// @file faust_shim.h
/// @brief The dsp / Meta / UI bases FAUST-generated C++ needs, and a real-time wrapper over them.
// SPDX-License-Identifier: MIT
// Copyright 2026 MuTap contributors

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>

// FAUST-generated code (generated/*.hpp) is not ours and is compiled with
// MuTap's warning set; bracket every #include of it with these two macros
// rather than editing the generated files. faust_generated.h does so.
#if defined(__clang__)
#define MUTAP_FAUST_BEGIN_GENERATED                                                                                    \
    _Pragma("clang diagnostic push") _Pragma("clang diagnostic ignored \"-Wall\"")                                     \
        _Pragma("clang diagnostic ignored \"-Wextra\"") _Pragma("clang diagnostic ignored \"-Wpedantic\"")             \
            _Pragma("clang diagnostic ignored \"-Wconversion\"") _Pragma("clang diagnostic ignored \"-Wshadow\"")      \
                _Pragma("clang diagnostic ignored \"-Wunused-parameter\"")                                             \
                    _Pragma("clang diagnostic ignored \"-Wabsolute-value\"")
#define MUTAP_FAUST_END_GENERATED _Pragma("clang diagnostic pop")
#elif defined(__GNUC__)
// GCC rejects (-Wpragmas) a pragma naming anything but a warning option, so
// the groups are spelled out: what MuTap's -Wall -Wextra -Wpedantic
// -Wconversion -Wshadow can raise on FAUST's output.
#define MUTAP_FAUST_BEGIN_GENERATED                                                                                    \
    _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wunused-parameter\"")                            \
        _Pragma("GCC diagnostic ignored \"-Wunused-variable\"")                                                        \
            _Pragma("GCC diagnostic ignored \"-Wunused-but-set-variable\"")                                            \
                _Pragma("GCC diagnostic ignored \"-Wunused-function\"")                                                \
                    _Pragma("GCC diagnostic ignored \"-Wconversion\"")                                                 \
                        _Pragma("GCC diagnostic ignored \"-Wsign-conversion\"")                                        \
                            _Pragma("GCC diagnostic ignored \"-Wfloat-conversion\"")                                   \
                                _Pragma("GCC diagnostic ignored \"-Wshadow\"")                                         \
                                    _Pragma("GCC diagnostic ignored \"-Wpedantic\"")                                   \
                                        _Pragma("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")                    \
                                            _Pragma("GCC diagnostic ignored \"-Wsign-compare\"")                       \
                                                _Pragma("GCC diagnostic ignored \"-Wmisleading-indentation\"")
#define MUTAP_FAUST_END_GENERATED _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
#define MUTAP_FAUST_BEGIN_GENERATED __pragma(warning(push, 0))
#define MUTAP_FAUST_END_GENERATED __pragma(warning(pop))
#else
#define MUTAP_FAUST_BEGIN_GENERATED
#define MUTAP_FAUST_END_GENERATED
#endif

/// MuTap's glue for its vendored FAUST code (third_party/faust/). The
/// generated classes are emitted into this namespace (`faust -ns
/// mutap_faust`), so they bind to the bases declared here.
namespace mutap_faust {

    // The three names below and the UI callbacks are FAUST's interface, which
    // the generated code calls by these exact (camelCase) names.
    // NOLINTBEGIN(readability-identifier-naming)

    /// Base of every generated DSP class (FAUST's `dsp`). The generated
    /// classes declare their own virtual interface; nothing here is
    /// overridden, so this only makes the destructor virtual.
    class dsp {
      public:
        dsp()                      = default;
        dsp(const dsp&)            = default;
        dsp& operator=(const dsp&) = default;
        virtual ~dsp()             = default;
    };

    /// Metadata sink (FAUST's `Meta`): the generated `metadata()` reports
    /// library names, versions and licenses through it. Ignored here.
    struct Meta {
        void declare(const char* /*key*/, const char* /*value*/) noexcept {}
    };

    /// Parameter sink (FAUST's `UI`): records each settable control's label
    /// and zone in a fixed-capacity table. Bargraphs (outputs) are ignored.
    /// The labels the generated code passes are string literals, so the table
    /// keeps views of them.
    class UI {
      public:
        static constexpr std::size_t k_capacity = 32;

        /// One recorded control. Exactly one of the two zones is non-null:
        /// the one matching the FAUSTFLOAT the class was included with.
        struct entry {
            std::string_view label;
            float*           zone_f32 = nullptr;
            double*          zone_f64 = nullptr;
        };

        void openVerticalBox(const char* /*label*/) noexcept {}
        void openHorizontalBox(const char* /*label*/) noexcept {}
        void openTabBox(const char* /*label*/) noexcept {}
        void closeBox() noexcept {}

        template <class T>
        void declare(T* /*zone*/, const char* /*key*/, const char* /*value*/) noexcept {}

        template <class T>
        void addButton(const char* label, T* zone) noexcept {
            record(label, zone);
        }
        template <class T>
        void addCheckButton(const char* label, T* zone) noexcept {
            record(label, zone);
        }
        template <class T>
        void addVerticalSlider(const char* label, T* zone, T /*init*/, T /*lo*/, T /*hi*/, T /*step*/) noexcept {
            record(label, zone);
        }
        template <class T>
        void addHorizontalSlider(const char* label, T* zone, T /*init*/, T /*lo*/, T /*hi*/, T /*step*/) noexcept {
            record(label, zone);
        }
        template <class T>
        void addNumEntry(const char* label, T* zone, T /*init*/, T /*lo*/, T /*hi*/, T /*step*/) noexcept {
            record(label, zone);
        }
        template <class T>
        void addHorizontalBargraph(const char* /*label*/, T* /*zone*/, T /*lo*/, T /*hi*/) noexcept {}
        template <class T>
        void addVerticalBargraph(const char* /*label*/, T* /*zone*/, T /*lo*/, T /*hi*/) noexcept {}

        /// @return the recorded controls, in the order the DSP declared them.
        [[nodiscard]] const entry* begin() const noexcept { return m_entries.data(); }
        [[nodiscard]] const entry* end() const noexcept { return m_entries.data() + m_count; }
        /// @return true if the DSP declared more than k_capacity controls.
        [[nodiscard]] bool overflowed() const noexcept { return m_overflowed; }

      private:
        void record(const char* label, float* zone) noexcept { push(entry{label, zone, nullptr}); }
        void record(const char* label, double* zone) noexcept { push(entry{label, nullptr, zone}); }
        void push(const entry& e) noexcept {
            if (m_count == k_capacity) {
                m_overflowed = true;
                return;
            }
            m_entries[m_count] = e;
            ++m_count;
        }

        std::array<entry, k_capacity> m_entries{};
        std::size_t                   m_count      = 0;
        bool                          m_overflowed = false;
    };

    // NOLINTEND(readability-identifier-naming)

    /// A FAUST-generated DSP behind the house real-time contract: the
    /// constructor allocates (the instance lives on the heap; the larger ones
    /// are hundreds of KB) and may throw; every other member is noexcept and
    /// allocation-free.
    ///
    /// @tparam Dsp     a generated class, e.g. mutap_faust::dattorro_f32
    /// @tparam Sample  the FAUSTFLOAT that class was included with (float for
    ///                 the _f32 classes, double for the _f64 ones, as
    ///                 faust_generated.h does)
    ///
    /// Not thread-safe to construct concurrently with another instance of the
    /// same class: FAUST's init() (re)fills class-static tables (the
    /// oscillators' sine tables) shared by every instance.
    template <class Dsp, class Sample>
    class faust_block {
      public:
        static_assert(std::is_same_v<Sample, float> || std::is_same_v<Sample, double>, "FAUSTFLOAT is float or double");
        static_assert(std::is_base_of_v<dsp, Dsp>, "Dsp must be a FAUST class generated into mutap_faust");
        static_assert(std::is_invocable_v<decltype(&Dsp::compute), Dsp&, int, Sample**, Sample**>,
                      "Sample must be the FAUSTFLOAT the Dsp was included with");

        /// Allocate the DSP, initialise it at `sample_rate` (parameters at
        /// their declared defaults, state cleared) and index its controls.
        /// @throws std::bad_alloc, or std::length_error if the DSP declares more
        ///         than UI::k_capacity controls.
        explicit faust_block(int sample_rate)
            : m_dsp{std::make_unique<Dsp>()} {
            m_dsp->init(sample_rate);
            m_inputs  = m_dsp->getNumInputs();
            m_outputs = m_dsp->getNumOutputs();
            UI ui;
            m_dsp->buildUserInterface(&ui);
            if (ui.overflowed()) {
                throw std::length_error("mutap_faust::faust_block: more controls than UI::k_capacity");
            }
            for (const UI::entry& e : ui) {
                Sample* const zone = zone_of(e);
                if (zone != nullptr) {
                    m_params[m_param_count] = param{e.label, zone};
                    ++m_param_count;
                }
            }
        }

        /// Set the control labelled `label` (the nentry name in the .dsp).
        /// Takes effect from the next process() block.
        /// @return false, changing nothing, if there is no such control.
        bool set(std::string_view label, Sample value) noexcept {
            for (std::size_t i = 0; i < m_param_count; ++i) {
                if (m_params[i].label == label) {
                    *m_params[i].zone = value;
                    return true;
                }
            }
            return false;
        }

        /// Process `n` frames: in[0..num_inputs()), out[0..num_outputs()).
        /// @pre in and out hold num_inputs() / num_outputs() non-aliasing
        ///      channel pointers of at least n samples each; n >= 0.
        void process(const Sample* const* in, Sample* const* out, int n) noexcept {
            // FAUST's compute() takes non-const pointers but only reads the inputs.
            m_dsp->compute(n, const_cast<Sample**>(in), const_cast<Sample**>(out));
        }

        /// Mono-in helper. @pre num_inputs() == 1.
        void process_mono(const Sample* in, Sample* const* out, int n) noexcept {
            const Sample* const ins[1] = {in};
            process(ins, out, n);
        }

        /// Mono-in, mono-out helper. @pre num_inputs() == 1 and num_outputs() == 1.
        void process_mono(const Sample* in, Sample* out, int n) noexcept {
            Sample* const outs[1] = {out};
            process_mono(in, outs, n);
        }

        /// Clear the DSP's state (delay lines, filters); controls keep their values.
        void clear() noexcept { m_dsp->instanceClear(); }

        [[nodiscard]] int num_inputs() const noexcept { return m_inputs; }
        [[nodiscard]] int num_outputs() const noexcept { return m_outputs; }

        /// The wrapped instance, for anything the wrapper does not expose.
        [[nodiscard]] Dsp& instance() noexcept { return *m_dsp; }

      private:
        struct param {
            std::string_view label;
            Sample*          zone = nullptr;
        };

        static Sample* zone_of(const UI::entry& e) noexcept {
            if constexpr (std::is_same_v<Sample, float>) {
                return e.zone_f32;
            }
            else {
                return e.zone_f64;
            }
        }

        std::unique_ptr<Dsp>              m_dsp;
        std::array<param, UI::k_capacity> m_params{};
        std::size_t                       m_param_count = 0;
        int                               m_inputs      = 0;
        int                               m_outputs     = 0;
    };

} // namespace mutap_faust
