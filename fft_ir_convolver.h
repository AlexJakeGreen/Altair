// fft_ir_convolver.h
const size_t idx = (wr_idx_ + K_ - k) % K_;
// tmp = Xhist[idx] * H_[k]
arm_cmplx_mult_cmplx_f32(Xhist_[idx].data(), H_[k].data(), tmp_.data(), N_);
// Y += tmp
arm_add_f32(tmp_.data(), Y_.data(), Y_.data(), 2 * N_);
}


// IFFT(Y)
arm_cfft_f32(&cfft_, Y_.data(), /*ifft=*/1, /*bitrev=*/1);


// Scale by 1/N (CMSIS CFFT doesn't scale the inverse)
const float invN = 1.0f / static_cast<float>(N_);
arm_scale_f32(Y_.data(), invN, Y_.data(), 2 * N_);


// Overlap‑save: discard first L, output last L real parts
for(size_t n = 0; n < L_; ++n)
{
const size_t idx = L_ + n;
out[n] = Y_[2 * idx + 0]; // real
}


// advance ring index
wr_idx_ = (wr_idx_ + 1) % K_;
}


size_t Hop() const { return L_; }
size_t FFTSize() const { return N_; }
size_t NumPartitions() const { return K_; }


private:
bool inited_ = false;
size_t L_ = 0; // hop size (block size)
size_t N_ = 0; // FFT size = 2L
size_t K_ = 0; // number of partitions


arm_cfft_instance_f32 cfft_{};


std::vector<std::vector<float>> H_; // [K][2N] complex spectra
std::vector<std::vector<float>> Xhist_; // [K][2N] complex spectra ring buffer
std::vector<float> Y_; // [2N] complex accumulator
std::vector<float> tmp_; // [2N] complex temp
std::vector<float> frame_; // [2N] complex time buffer
size_t wr_idx_ = 0; // ring write index
};


/* ===================== Usage in your project =====================


1) Include this header and link CMSIS-DSP (already present in Daisy projects).


2) Pick a hop size equal to your audio block size. Example: 256.
Set Daisy block size to that:
hw.SetAudioBlockSize(256);


3) Build the convolver at init using your IR array:


#include "fft_ir_convolver.h"
PartitionedFFTConvolverFDL g_ir;
size_t g_hop = 256; // must be power of two
size_t g_ir_index = 2; // choose from your ir_collection


void setup_ir_fft() {
const auto &ir = ir_collection[g_ir_index];
g_ir.Init(ir.data(), ir.size(), g_hop);
}


4) In AudioCallback, process in blocks (size == hop). Replace mIR.Process:


void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
// assert(size == g_ir.Hop());
static std::vector<float> tmp_in, tmp_out;
if(tmp_in.size() != size) { tmp_in.resize(size); tmp_out.resize(size); }


// mono in
for(size_t i = 0; i < size; ++i) tmp_in[i] = in[0][i];


g_ir.ProcessBlock(tmp_in.data(), tmp_out.data());


for(size_t i = 0; i < size; ++i) {
float y = tmp_out[i];
out[0][i] = y; out[1][i] = y;
}
}


5) If you need per‑sample mixing with your GRU output, fill tmp_in[] with the GRU output for the whole block, then call ProcessBlock once per callback.


================================================================= */
