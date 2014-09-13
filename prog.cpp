#include <csignal>
#include <cstring>
#include <cwchar>

#include <deque>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <poll.h>
#include <termcap.h>
#include <termios.h>
#include <unistd.h>


using std::cerr;
using std::endl;
using std::deque;
using std::string;
using std::vector;
using std::perror;
using std::ostringstream;


#define NUM_POLL_FDS 1


class App
{
public:
	App();
	~App();
	bool init();
	bool loop();
	void log(const string& msg);
	bool on_char(char ch);
private:
	bool init_termcap();
	bool init_termios();
	bool init_poll();
	bool init_screen();

	void restore_termios();

private:
	class Terminal
	{
	public:
		Terminal();
		bool init(int out_fd);

	public:
		int get_screen_lines()   const { return this->scr_lines;   }
		int get_screen_columns() const { return this->scr_columns; }

		void put_text(const std::string& s);
		void put_text(const char* s, size_t len);

		void clear_screen()
		{
			cap_write(this->cl, this->scr_lines);
		}
		void carriage_return()
		{
			cap_write(this->cr, 1);
		}
		void clear_to_end_of_line()
		{
			cap_write(this->ce, 1);
		}
		void move_cursor(int row, int col)
		{
			cap_write_param(this->cm, 1, col, row);
		}
		void scroll_forward()
		{
			cap_write(this->sf, 1);
		}
		void change_scroll_region(int first, int last)
		{
			cap_write_param(this->cs, 1, last, first);
		}

	private:
		void cap_write(const char* cap, int n);
		void cap_write_param(const char* cap, int n, int p1, int p2);
		static int write_char(int ch);

	private:
		bool get_capability(const char* label, const char* name, const char** pptr);
	private:
		static int g_WriteFd;
	private:
		int out_fd;

		// capabilities.
		const char* cm;
		const char* sf;
		const char* cs;
		const char* cl;
		const char* cr;
		const char* ce;

		// screen size.
		int scr_lines;
		int scr_columns;

		// capability buffers.
		char tent_buf[1024];
		char tget_buf[1024];
		char* tget_ptr;
	};

	class WorkBuffer
	{
	public:
		WorkBuffer();
	public:
		bool init(Terminal* term);
		void reset();
		void clear_line();
		void put_char(char ch);
		void put_enter();
		int  delete_char();

		int get_width() const { return width; }
	private:
		void commit_char(const string& char_text, int width);
	private:
		Terminal* term;
		int       input_count;

		int            width;
		string         prompt;
		int            prompt_width;
		vector<string> chars;
		vector<int>    widths;
		string         in_buf;
	};

private:
	// misc.
	int  in_fd;
	int  out_fd;
	int  wait_count;
	bool loop_cont;

	WorkBuffer wkbuf;
	Terminal   term;

	// termios.
	bool           tio_initialized;
	struct termios tio_orig;
	struct termios tio_cur;

	// poll.
	struct pollfd poll_fds[NUM_POLL_FDS];
};


App::App()
	// misc.
	: wait_count(1)
	, loop_cont(true)

	// termios.
	, tio_initialized(false)
{
}


App::~App()
{
	this->restore_termios();
}


bool App::init()
{
	this->in_fd  = 0; // stdin.
	this->out_fd = 1; // stdout.

	if( !this->term.init(this->out_fd) )
	{
		return false;
	}

	if( !this->wkbuf.init(&this->term) )
	{
		return false;
	}

	if( !this->init_termios() )
	{
		return false;
	}

	if( !this->init_poll() )
	{
		return false;
	}

	if( !this->init_screen() )
	{
		return false;
	}

	return true;
}


bool App::init_termios()
{
	int ret;

	ret = tcgetattr(this->in_fd, &this->tio_orig);
	if( ret != 0 )
	{
		perror("tcgetattr");
		return false;
	}
	this->tio_initialized = true;

	this->tio_cur            =  this->tio_orig;
	this->tio_cur.c_iflag    &= ~ISTRIP;
	this->tio_cur.c_lflag    &= ~(ECHO | ICANON | ISIG);
	this->tio_cur.c_cc[VMIN] =  0; // use non-blocking read.

	ret = tcsetattr(this->in_fd, TCSANOW, &this->tio_cur);
	if( ret != 0 )
	{
		perror("tcsetattr");
		return false;
	}

	return true;
}


bool App::init_poll()
{
	memset(this->poll_fds, 0, sizeof(this->poll_fds[0]) * NUM_POLL_FDS);
	this->poll_fds[0].fd     = this->in_fd;
	this->poll_fds[0].events = POLLIN;

	return true;
}


bool App::init_screen()
{
	this->term.clear_screen();
	this->term.move_cursor(this->term.get_screen_lines() - 1, 0);

	this->wkbuf.reset();

	return true;
}


void App::restore_termios()
{
	if( !this->tio_initialized )
	{
		return;
	}

	int ret;
	ret = tcsetattr(this->in_fd, TCSANOW, &this->tio_orig);
	if( ret != 0 )
	{
		perror("tcsetattr(restore)");
	}
}


bool App::loop()
{
	while( this->loop_cont )
	{
		int timeout_ms = 1000 * 1;
		int n = poll(this->poll_fds, NUM_POLL_FDS, timeout_ms);

		if( n == -1 )
		{
			perror("poll");
			return false;
		}
		if( n == 0 )
		{
			ostringstream ost;
			ost << "waiting for input (" << this->wait_count << ") ...";
			this->log(ost.str());
			++ this->wait_count;
			continue;
		}

		char ch;
		int ret = read(this->poll_fds[0].fd, &ch, 1);
		if( ret == -1 )
		{
			perror("read");
			return false;
		}
		if( ret == 0 )
		{
			// EOF.
			break;
		}
		if( !this->on_char(ch) )
		{
			return false;
		}
	}

	return true;
}


void App::log(const string& msg)
{
	int scr_lines = this->term.get_screen_lines();
	this->term.change_scroll_region(0, scr_lines - 2);
	this->term.move_cursor(scr_lines - 2, 0);
	this->term.scroll_forward();
	write(this->out_fd, msg.data(), msg.size());

	this->term.change_scroll_region(0, scr_lines - 1);
	this->term.move_cursor(scr_lines - 1, this->wkbuf.get_width());
}


bool App::on_char(char ch)
{
	switch( ch )
	{
		case 3: // ^C.
		case 4: // ^D.
		{
			write(this->out_fd, "\n", 1);
			this->loop_cont = false;
			break;
		}

		case 8:    // ^H.
		case 0x7F: // DEL.
		{
			int del_width = this->wkbuf.delete_char();
			for( int i=0; i<del_width; ++i )
			{
				// shoud use delete-one-char (dc), etc. capabilities?
				write(this->out_fd, "\x08 \x08", 3);
			}
			break;
		}

		case '\n': // 10:^J.
		{
			// see also: c_iflag/ICRNL.
			this->wkbuf.put_enter();
			break;
		}

		case 21: // ^U.
		{
			this->term.carriage_return();
			this->term.clear_to_end_of_line();
			this->wkbuf.clear_line();
			break;
		}

		case 26: // ^Z.
		{
			int ret;

			kill(getpid(), SIGSTOP);

			// after SIGCONT.
			ret = tcsetattr(this->in_fd, TCSANOW, &this->tio_cur);
			if( ret != 0 )
			{
				perror("tcsetattr(cont)");
				return false;
			}
			break;
		}

		default:
		{
			this->wkbuf.put_char(ch);
			break;
		}
	}

	return true;
}


int App::Terminal::g_WriteFd;


App::Terminal::Terminal()
	: out_fd(-1)
	, tget_ptr(&tget_buf[0])
{
}


bool App::Terminal::init(int out_fd)
{
	int ret;

	this->out_fd             = out_fd;
	App::Terminal::g_WriteFd = out_fd;

	ret = tgetent(tent_buf, getenv("TERM"));
	if( ret != 1 )
	{
		cerr << "tgetent failed (" << ret << ")." << endl;
		return false;
	}

	this->scr_lines   = tgetnum("li");
	this->scr_columns = tgetnum("co");

	struct {
		const char*  label;
		const char*  name;
		const char** pptr;
		bool         required;
	} caps[] = {
		{ "cursor move",            "cm", &this->cm, true},
		{ "scroll forward",         "sf", &this->sf, true},
		{ "change scroll region",   "cs", &this->cs, true},
		{ "clear screen",           "cl", &this->cl, true},
		{ "carriage return",        "cr", &this->cr, false},
		{ "clear to end of line",   "ce", &this->ce, true},
		// { "cursor position format", "u6", &this->u6, false},
		// { "query cursor position",  "u7", &this->u7, false},
		// { "delete one character",   "dc", &this->dc, false},
		{ NULL, NULL, NULL }
	};

	for( int i=0; caps[i].label != NULL; ++i )
	{
		if( !this->get_capability(caps[i].label, caps[i].name, caps[i].pptr) )
		{
			if( !caps[i].required )
			{
				// optional.
				continue;
			}
			return false;
		}
	}

	if( this->cr == NULL )
	{
		this->cr = "\r";
	}

	return true;
}


bool App::Terminal::get_capability(const char* label, const char* name, const char** pptr)
{
	const char* ptr;

	ptr = tgetstr(name, &this->tget_ptr);
	if( ptr == NULL )
	{
		cerr << "tgetstr(" << name << ":" << label << ") failed." << endl;
		*pptr = NULL;
		return false;
	}

	*pptr = ptr;
	return true;
}


void App::Terminal::put_text(const std::string& s)
{
	write(this->out_fd, s.data(), s.size());
}


void App::Terminal::put_text(const char* s, size_t len)
{
	write(this->out_fd, s, len);
}


void App::Terminal::cap_write(const char* cap, int n)
{
	tputs(cap, n, &App::Terminal::write_char);
}

void App::Terminal::cap_write_param(const char* cap, int n, int p1, int p2)
{
	tputs(tgoto(cap, p1, p2), n, &App::Terminal::write_char);
}

int App::Terminal::write_char(int ch)
{
	// NOTE: out_fd is taken from static (class-) variable.
	// Because it is not able to distinguish caller instance by
	// parameter via tputs(3) interface.
	char buf[1] = { static_cast<char>(ch) };
	return write(App::Terminal::g_WriteFd, &buf, 1);
}


App::WorkBuffer::WorkBuffer()
	: term(NULL)
	, input_count(1)
{
}


bool App::WorkBuffer::init(Terminal* term)
{
	this->term = term;

	return true;
}


void App::WorkBuffer::reset()
{
	ostringstream ost;
	ost << "input." << this->input_count << "> ";

	this->prompt       = ost.str();
	this->prompt_width = this->prompt.size(); // TODO: wcswidth.

	clear_line();
}

void App::WorkBuffer::clear_line()
{
	this->chars.clear();
	this->widths.clear();
	this->in_buf.erase();

	this->width = this->prompt_width;
	this->term->put_text(this->prompt);
}


void App::WorkBuffer::put_char(char ch)
{
	if( static_cast<unsigned char>(ch) < 0x20 && this->in_buf.empty() )
	{
		// control chars.
		// 0x7F(DEL) is also ctrl-char. but it is processed at other place.

		ostringstream ost;
		int n = static_cast<unsigned char>(ch);

		// sprintf "[%02x]"
		ost << '[' << std::hex << std::setw(2) << std::setfill('0') << n << ']';

		string text  = ost.str();
		int    width = text.size(); // since us-ascii only.
		this->commit_char(text, width);

		return;
	}

	this->in_buf.push_back(ch);

	while( !this->in_buf.empty() )
	{
		wchar_t wc;
		int len;
		len = mbtowc(&wc, this->in_buf.data(), this->in_buf.size());
		if( len > 0 )
		{
			int wid = wcwidth(wc);
			this->commit_char(this->in_buf.substr(0, len), wid);
			this->in_buf.erase(0, len);
		}else
		{
			mbtowc(NULL, NULL, 0); // reset internal shift state.
			if( this->in_buf.size() >= MB_CUR_MAX )
			{
				this->in_buf.erase(0, 1);
				continue;
			}
			break;
		}
	}
}


void App::WorkBuffer::put_enter()
{
	this->term->put_text("\n", 1);
	if( !this->chars.empty() )
	{
		++ this->input_count;
	}
	this->reset();
}


void App::WorkBuffer::commit_char(const string& char_text, int width)
{
	if( this->width + width < this->term->get_screen_columns() )
	{
		this->chars.push_back(char_text);
		this->widths.push_back(width);

		this->width += width;
		this->term->put_text(char_text);
	}
}


static void string_pop_back(string& s)
{
	// string::pop_back() requires c++11.
	s.erase(s.size()-1, 1);
}


int App::WorkBuffer::delete_char()
{
	if( !this->in_buf.empty() )
	{
		string_pop_back(this->in_buf);
		return 0;
	}

	if( this->chars.empty() )
	{
		return 0;
	}

	int ret = this->widths.back();

	this->chars.pop_back();
	this->widths.pop_back();

	this->width -= ret;

	return ret;
}


int main()
{
	// for wchar.
	setlocale(LC_CTYPE, "");

	App app;
	if( !app.init() )
	{
		return 1; // EXIT_FAILURE.
	}

	bool ret = app.loop();
	if( !ret )
	{
		return 1; // EXIT_FAILURE.
	}

	return 0; // EXIT_SUCCESS.
}
