#!/usr/bin/env python3
"""Assemble and execute notebooks/itu_comparison.ipynb.

The companion to notebooks/itu_compliance.ipynb: the SAME ITU scenarios
and masks, but overlaying all three cancellers (MuTap, WebRTC AEC3,
Speex MDF) instead of proving MuTap alone. It renders the JSON written by
the C++ dump (tools/notebook/itu_compare_dump.cpp, built by the
comparison harness), which drives every subject through the compliance
machinery in tests/support. Prose and the fairness caveats live in
docs/aec-comparison.md.

Unlike build_itu_compliance.py this does NOT compile anything in-notebook
(the dump needs the third-party cancellers built); it reads the committed
bench/compare/results/itu_comparison.json. Regenerate that first with:

    cmake --build build --target itu_compare_dump   # needs -DMUTAP_BUILD_COMPARE + subjects
    ./build/bench/compare/itu_compare_dump > bench/compare/results/itu_comparison.json

Then: python3 tools/notebook/build_itu_comparison.py
Requires: nbformat, nbclient, numpy, matplotlib.
"""
import pathlib

import nbformat as nbf
from nbclient import NotebookClient

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

nb = nbf.v4.new_notebook()
nb.metadata.kernelspec = {"display_name": "Python 3", "language": "python", "name": "python3"}
cells = []
md = lambda s: cells.append(nbf.v4.new_markdown_cell(s))
code = lambda s: cells.append(nbf.v4.new_code_cell(s))

md("""# MuTap vs. WebRTC AEC3 and Speex MDF — on the ITU battery

The [ITU compliance notebook](itu_compliance.ipynb) proves MuTap's echo
canceller against the in-force ITU-T automotive/hands-free requirements.
This companion runs the **same scenarios, signals, paths and meters**
(`tests/support/itu_chain.h` — P.501 CSS, the image-source car cabin and
studio, the dBm0 ERL reader) through **all three cancellers** so you can
see how WebRTC AEC3 and Speex MDF fare on the same masks.

Every subject is a black box: far-end + microphone in, cleaned send out
([`tools/notebook/itu_compare_dump.cpp`](../tools/notebook/itu_compare_dump.cpp)).
Full methodology, subjects, and the fairness caveats — float32 vs
float64, AEC3 running echo-canceller-only, echo within the filter's
modelling capacity, why raw ERLE is MuTap's axis and not AEC3's — are in
[`docs/aec-comparison.md`](../docs/aec-comparison.md). The certified
compliance notebook stays MuTap-only and remains the CI-gated authority;
this is the comparison view.""")

code("""import json, pathlib
import numpy as np
import matplotlib.pyplot as plt

here = pathlib.Path.cwd()
root = here.parent if here.name == 'notebooks' else here
D = json.load(open(root / 'bench' / 'compare' / 'results' / 'itu_comparison.json'))
SUBJECTS = D['subjects']
COL = {'mutap': '#1f77b4', 'webrtc': '#d62728', 'speex': '#2ca02c'}
LBL = {'mutap': 'MuTap', 'webrtc': 'WebRTC AEC3', 'speex': 'Speex MDF'}
print('subjects:', SUBJECTS)

def trace_xy(tr):
    v = np.array(tr['v'], float)
    t = tr['t0'] + tr['dt'] * np.arange(len(v))
    return t, v""")

md("""## 1. Initial convergence, quiet (§11.11.4)

ERL against time from a cold start, cabin path, P.501 CSS at −16 dBm0.
The requirement is ≥ 40 dB by 1200 ms, held; MuTap's margin target is
≥ 40 dB by **600 ms**. The dashed line is the 40 dB requirement.""")

code("""fig, axes = plt.subplots(1, 2, figsize=(13, 4.2), sharey=True)
for ax, fs in zip(axes, ['48000', '16000']):
    for s in SUBJECTS:
        c = D[fs]['convergence'].get(s)
        if not c: continue
        t, v = trace_xy(c)
        ax.plot(t, v, color=COL[s], label=LBL[s], lw=1.8)
    ax.axhline(40, ls='--', color='k', alpha=0.6, lw=1)
    ax.axvline(1.2, ls=':', color='k', alpha=0.4)
    ax.set_title(f'{int(fs)//1000} kHz'); ax.set_xlabel('time (s)'); ax.grid(alpha=0.3)
axes[0].set_ylabel('ERL (dB) — higher = deeper echo removal'); axes[0].legend(loc='lower right')
fig.suptitle('Convergence (quiet) — ERL(t) vs the §11.11.4 requirement (40 dB, dashed)', y=1.02)
plt.tight_layout(); plt.show()""")

md("""MuTap climbs past the others to its deep steady floor; AEC3 locks
almost instantly but plateaus far shallower (it optimises for perceived
echo, not ERL depth — see the AECMOS table in the doc); Speex sits
between, converging slowly to a mid depth.""")

md("""## 2. Re-convergence after an abrupt path change (§11.11.6/7)

Converge on the cabin, then swap to the studio path mid-run (vertical
line). How fast does each canceller recover its echo suppression?""")

code("""fig, axes = plt.subplots(1, 2, figsize=(13, 4.2), sharey=True)
for ax, fs in zip(axes, ['48000', '16000']):
    rc = D[fs]['reconvergence']; swap = rc['swap_s']
    for s in SUBJECTS:
        c = rc['curves'].get(s)
        if not c: continue
        t, v = trace_xy(c)
        ax.plot(t, v, color=COL[s], label=LBL[s], lw=1.8)
    ax.axvline(swap, ls='--', color='k', alpha=0.6); ax.set_title(f'{int(fs)//1000} kHz')
    ax.set_xlabel('time (s)'); ax.grid(alpha=0.3)
axes[0].set_ylabel('ERL (dB)'); axes[0].legend(loc='lower right')
fig.suptitle('Re-convergence — ERL(t); dashed line = cabin→studio path swap', y=1.02)
plt.tight_layout(); plt.show()""")

md("""AEC3's fast delay/filter re-estimation is its home turf; MuTap's
chain recovers more slowly to a much deeper floor (its rescue comparator
is tuned conservative, per docs/itu-compliance.md). The trade is depth
vs agility, made visible.""")

md("""## 3. Spectral echo attenuation vs the §11.11.3 WB mask

Converged single talk: the third-octave attenuation spectrum of the echo
vs the send output, overlaid on the P.1110/P.1120 wideband echo mask
(dashed — the requirement is to sit *above* it). Depth as a function of
frequency, the way the recs actually score echo.""")

code("""MASK_F = [100, 1300, 3450, 5200, 7500, 8000]; MASK_DB = [41, 41, 46, 46, 37, 37]
def mask_at(f):
    return np.interp(np.log(f), np.log(MASK_F), MASK_DB)
fig, axes = plt.subplots(1, 2, figsize=(13, 4.2), sharey=True)
for ax, fs in zip(axes, ['48000', '16000']):
    for s in SUBJECTS:
        sp = D[fs]['spectral'].get(s)
        if not sp: continue
        ax.semilogx(sp['f'], sp['atten_db'], color=COL[s], label=LBL[s], lw=1.8, marker='o', ms=3)
    ff = np.array(D[fs]['spectral'][SUBJECTS[0]]['f'])
    ax.semilogx(ff, [mask_at(f) for f in ff], 'k--', alpha=0.6, label='WB mask (§11.11.3)')
    ax.set_title(f'{int(fs)//1000} kHz'); ax.set_xlabel('frequency (Hz)'); ax.grid(alpha=0.3, which='both')
axes[0].set_ylabel('echo attenuation (dB) — above the mask = pass'); axes[0].legend(fontsize=8)
fig.suptitle('Spectral echo attenuation vs the WB mask', y=1.02)
plt.tight_layout(); plt.show()""")

md("""MuTap clears the mask by 15–40 dB across the band; Speex clears it
comfortably too; AEC3 rides much closer, dipping toward the mask at the
band edges — again the depth-vs-robustness split, now resolved by
frequency.""")

md("""## 4. Double talk — the real ITU AM-FM comb method (§11.12)

The proper measurement (corrected from the earlier near-keep proxy):
converge on CSS, then far end on the receive comb and near end on the
orthogonal send comb. **Echo loss** = how far the residual echo sits
below the clean far end, worst receive band, quiet near end (≥ 27 dB
required). **Send attenuation** = how much the *near end* is ducked
during loud double talk, worst send band (≤ 3 dB required — lower is
better duplex).""")

code("""fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 4.2))
x = np.arange(len(SUBJECTS)); w = 0.36
for i, fs in enumerate(['48000', '16000']):
    ax1.bar(x + i*w, [D[fs]['dt_comb'].get(s, {}).get('echo_loss_db', 0) for s in SUBJECTS], w,
            label=f'{int(fs)//1000} kHz', color=['#4c78a8', '#a0c4e0'][i])
    ax2.bar(x + i*w, [D[fs]['dt_comb'].get(s, {}).get('send_atten_db', 0) for s in SUBJECTS], w,
            label=f'{int(fs)//1000} kHz', color=['#e45756', '#f2a6a5'][i])
ax1.axhline(27, ls='--', color='k', alpha=0.6); ax1.annotate('≥27 dB req', (0, 28), fontsize=8)
ax2.axhline(3, ls='--', color='k', alpha=0.6); ax2.annotate('≤3 dB req', (0, 3.4), fontsize=8)
for ax, ttl, yl in ((ax1, 'DT echo loss, worst band (↑ better)', 'echo loss (dB)'),
                    (ax2, 'DT near-end ducking, worst band (↓ better)', 'send attenuation (dB)')):
    ax.set_xticks(x + w/2); ax.set_xticklabels([LBL[s] for s in SUBJECTS])
    ax.set_title(ttl); ax.set_ylabel(yl); ax.grid(axis='y', alpha=0.3); ax.legend()
plt.tight_layout(); plt.show()""")

md("""**Reading it.** MuTap's echo loss (37.8 / 38.0 dB) reproduces the
certified compliance figure to a few tenths — the black-box path measures
the same chain the suite gates. Speex is comparably deep (its double-talk
detector freezes adaptation). **AEC3 reads ~0 dB echo loss on this
signal** — the orthogonal AM-FM comb is a synthetic ITU probe that its
speech-tuned nonlinear model does not cancel, so that bar reflects signal
mismatch, *not* AEC3's speech-echo capability. The **send-attenuation**
panel is the architecture-representative double-talk result: MuTap and
Speex leave the near end essentially untouched (≤ 2 dB), while AEC3 ducks
it ~15 dB — its deliberate echo-first double-talk behaviour, consistent
with the AECMOS degradation scores in the doc.""")

md("""## 5. Steady-state ERL and send-path activation build-up

Left: converged single-talk ERL depth. Right: how fast the near-end
talker's onset reaches the send output (§11.11.8.1 build-up; ≤ 50 ms
required, dashed) — for a black-box canceller, how quickly it lets a
near-end talker through.""")

code("""fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 4.2))
x = np.arange(len(SUBJECTS)); w = 0.36
for i, fs in enumerate(['48000', '16000']):
    ax1.bar(x + i*w, [D[fs]['steady_erl'].get(s, 0) for s in SUBJECTS], w,
            label=f'{int(fs)//1000} kHz', color=['#4c78a8', '#a0c4e0'][i])
    ax2.bar(x + i*w, [min(D[fs]['activation'].get(s, 0), 60) for s in SUBJECTS], w,
            label=f'{int(fs)//1000} kHz', color=['#54a24b', '#a7d49b'][i])
ax2.axhline(50, ls='--', color='k', alpha=0.6); ax2.annotate('≤50 ms req', (0, 51), fontsize=8)
for ax, ttl, yl in ((ax1, 'Steady-state ERL (↑ deeper)', 'ERL (dB)'),
                    (ax2, 'Send activation build-up (↓ faster)', 'build-up time (ms)')):
    ax.set_xticks(x + w/2); ax.set_xticklabels([LBL[s] for s in SUBJECTS])
    ax.set_title(ttl); ax.set_ylabel(yl); ax.grid(axis='y', alpha=0.3); ax.legend()
plt.tight_layout(); plt.show()""")

md("""**Reading it all together.** Across the ITU battery the same split
holds: MuTap trades convergence and re-convergence speed for the deepest
steady echo removal, the widest mask margin, certified-grade double-talk
echo loss, and near-perfect near-end preservation; AEC3 locks fastest and
lets the near end through fastest but rides shallow and ducks the near end
hard under double talk; Speex is the classical middle. All three clear the
activation build-up requirement comfortably. The full numeric matrix, the
reciprocal AECMOS scoring, and every fairness caveat are in
[`docs/aec-comparison.md`](../docs/aec-comparison.md).""")

nb["cells"] = cells
client = NotebookClient(nb, timeout=180, kernel_name="python3",
                        resources={"metadata": {"path": str(REPO_ROOT / "notebooks")}})
client.execute()
out = REPO_ROOT / "notebooks" / "itu_comparison.ipynb"
nbf.write(nb, str(out))
print("wrote", out)
