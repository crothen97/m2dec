#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <memory>
#include "frames.h"
#include "filewrite.h"
#include "m2decoder.h"
#include "getopt.h"
#include "bitio.h"
#include "h264.h"

#ifdef __RENESAS_VERSION__
extern char InputFile[];
extern const int InputSize;
#endif /* __RENESAS_VERSION__ */

class option_t {
	uint8_t *input_data_;
	size_t input_len_;
	size_t pos_;
	int skipped_num_;
	M2Decoder::header_data_list_t headers_;
	FileWriter *fw_;
	M2Decoder::type_t codec_;
	int dpb_;
	bool force_exec_;
	bool dpb_emptify_;
	M2Decoder *dec_;
	static bool outfilename(char *infilename, char *outfilename, size_t size) {
		char *start;
		char *end;
		if (!(start = strrchr(infilename, '/')) && !(start = strrchr(infilename, '\\'))) {
			start = infilename;
		} else {
			start += 1;
		}
		end = strrchr(start, '.');
		if (end) {
			*end = '\0';
		}
		if (size <= strlen(start)) {
			return false;
		}
		sprintf(outfilename, "%s.out", start);
		return true;
	}
	FileWriter *file_writer_create(char *name, int mode) {
		char outfile[256];
		if (outfilename(name, outfile, sizeof(outfile))) {
			FileWriter *fw;
			if (mode == FileWriter::WRITE_MD5) {
				fw = new FileWriterMd5();
			} else {
				fw = new FileWriterRaw();
			}
			if (fw->set_file(outfile, false)) {
				return fw;
			} else {
				delete fw;
				return 0;
			}
		}
		return 0;
	}
	int reread_file_impl() {
		if (!headers_.empty()) {
			if (headers_.front().first != 0) {
				dec_bits_set_data(dec()->stream(), headers_.front().first, headers_.front().second, 0);
				headers_.pop_front();
				return 0;
			} else {
				headers_.clear();
				return -1;
			}
		} else if (pos_ < input_len_) {
			dec_bits_set_data(dec()->stream(), input_data_ + pos_, input_len_, 0);
			pos_ += input_len_;
			return 0;
		} else {
			return -1;
		}
	}
	static int reread_file(void *var) {
		return ((option_t *)var)->reread_file_impl();
	}
	void BlameUser() {
		fprintf(stderr,
			"Usage:\n"
			"\th264dec [-b] [-d <dpb_size>] [-o|O ] <infile>\n"
			"\t\t-b: Bypass DPB\n"
			"\t\t-d <dpb_size>: Specify number of DPB frames -1, 1..16 (default: -1(auto))\n"
			"\t\t-e: emptifiy DPB before next frames\n"
			"\t\t-f <skip_num>: Specify number of frames to be skipped\n"
			"\t\t-m: MPEG2 elementary input\n"
			"\t\t-o: RAW output\n"
			"\t\t-O: MD5 output\n"
			"\t\t-s: MPEG2 PS input\n"
			"\t\t-x: Mask SIGABRT on error."
			);
		exit(1);
	}
public:
	option_t(int argc, char *argv[])
		: pos_(0), skipped_num_(0), fw_(0), codec_(M2Decoder::MODE_NONE), dpb_(-1), force_exec_(false), dpb_emptify_(false), dec_(0) {
		FILE *fi;
		int opt;
		int filewrite_mode = FileWriter::WRITE_NONE;
		int skip_num = 0;
		while ((opt = getopt(argc, argv, "bd:ef:moOsx")) != -1) {
			switch (opt) {
			case 'b':
				dpb_ = 1;
				break;
			case 'd':
				dpb_ = strtol(optarg, 0, 0);
				if (32 < (unsigned)dpb_) {
					BlameUser();
					/* NOTREACHED */
				}
				break;
			case 'e':
				dpb_emptify_ = true;
				break;
			case 'f':
				skip_num = static_cast<int>(strtol(optarg, 0, 0));
				break;
			case 'm':
				codec_ = M2Decoder::MODE_MPEG2;
				break;
			case 'O':
				filewrite_mode = FileWriter::WRITE_MD5;
				break;
			case 'o':
				filewrite_mode = FileWriter::WRITE_RAW;
				break;
			case 's':
				codec_ = M2Decoder::MODE_MPEG2PS;
				break;
			case 'x':
				force_exec_ = true;
				break;
			default:
				BlameUser();
				/* NOTREACHED */
			}
		}
		if ((argc <= optind) ||	!(fi = fopen(argv[optind], "rb"))) {
			BlameUser();
			/* NOTREACHED */
		}
		if (codec_ == M2Decoder::MODE_NONE) {
			codec_ = detect_file(argv[optind]);
		}
		if (filewrite_mode != FileWriter::WRITE_NONE) {
			fw_ = file_writer_create(argv[optind], filewrite_mode);
		}
#ifdef __RENESAS_VERSION__
		input_data_ = (uint8_t *)InputFile;
		input_len_ = InputSize;
#else
		fseek(fi, 0, SEEK_END);
		input_len_ = ftell(fi);
		fseek(fi, 0, SEEK_SET);
		input_data_ = new uint8_t[input_len_];
		fread(input_data_, 1, input_len_, fi);
#endif
		fclose(fi);
		dec_ = new M2Decoder(codec_, 0, reread_file, this);
		if (skip_num != 0) {
			int skipped_bytes;
			skipped_num_ = dec_->skip_frames(input_data_, input_len_, skip_num, skipped_bytes, headers_);
			pos_ += skipped_bytes;
			fprintf(stderr, "Skip %d frames(%d bytes).\n", skipped_num_, skipped_bytes);
		}
	}
	~option_t() {
		if (dec_) {
			delete dec_;
		}
#ifndef __RENESAS_VERSION__
		delete[] input_data_;
#endif
		if (fw_) {
			delete fw_;
		}
	}
	size_t writeframe(const m2d_frame_t *frame) {
		if (fw_) {
			return fw_->writeframe(frame);
		}
		return 0;
	}
	M2Decoder *dec() {
		return dec_;
	}
	size_t input_len() const {
		return input_len_;
	}
	int skipped_num() const {
		return skipped_num_;
	}
	bool force_exec() const {
		return force_exec_;
	}
	bool dpb_emptify() const {
		return dpb_emptify_;
	}
};

#ifdef _M_IX86
#include <crtdbg.h>
#elif defined(__linux__)
#include <pthread.h>
#include <signal.h>
static void trap(int no)
{
	fprintf(stderr, "trap %d\n", no);
	exit(0);
}
#endif

static void write_frame(void *arg, m2d_frame_t&frame)
{
	((option_t *)arg)->writeframe(&frame);
}

int main(int argc, char *argv[])
{
	int err;

#ifdef _M_IX86
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_WNDW);
//	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_WNDW);
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF|_CRTDBG_LEAK_CHECK_DF);
#endif
	option_t opt(argc, argv);
	if (opt.input_len() <= 0) {
		return -1;
	}
#ifdef __linux__
	if (opt.force_exec()) {
		struct sigaction sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = trap;
		sigaction(SIGABRT, &sa, 0);
		sigaction(SIGSEGV, &sa, 0);
	}
#endif
	for (int i = 0; i < INT_MAX; ++i) {
		err = opt.dec()->decode(&opt, write_frame, opt.dpb_emptify());
		if (err < 0) {
			opt.dec()->decode_residual(&opt, write_frame);
			break;
		}
	}

#ifdef _M_IX86
	assert(_CrtCheckMemory());
#endif
	return (opt.force_exec()) ? 0 : ((err == -2) ? 0 : err);
}

