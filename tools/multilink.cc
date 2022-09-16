#include <stdio.h>
#include <fmt/format.h>
#include <filesystem>
#include <vector>
#include <fstream>
#include <sstream>

template<typename ...T>
void error(fmt::format_string<T...> fmt, T&&... args)
{
	fmt::print(stderr, fmt, args...);
	fputc('\n', stderr);
	exit(1);
}

std::vector<uint16_t> compare(const std::string& f1, const std::string& f2)
{
	if (std::filesystem::file_size(f1) != std::filesystem::file_size(f2))
		error("files {} and {} are not the same size! {} {}", f1, f2);

	std::vector<uint16_t> results;
	std::ifstream s1(f1);
	std::ifstream s2(f2);

	unsigned pos = 0;
	while (!s1.eof())
	{
		uint8_t b1 = s1.get();
		uint8_t b2 = s2.get();

		if (b1 != b2)
			results.push_back(pos);
		pos++;
	}

	return results;
}

std::vector<uint8_t> toBytestream(const std::vector<uint16_t>& differences)
{
	std::vector<uint8_t> results;
	uint16_t pos = 0;

	for (uint16_t diff : differences)
	{
		uint16_t delta = diff - pos;
		while (delta >= 0xff)
		{
			results.push_back(0xff);
			delta -= 0xff;
		}
		results.push_back(delta);

		pos = diff;
	}

	return results;
}

void emitw(std::ostream& s, uint16_t w)
{
	s.put(w & 0xff);
	s.put(w >> 8);
}

void align(std::ostream& s)
{
	while (s.tellp() & 127)
		s.put(0);
}

uint16_t paras(uint16_t value)
{
	return (value + 127) / 128;
}

unsigned roundup(unsigned value)
{
	return (value + 127) & ~127;
}

int main(int argc, char* const* argv)
{
	if ((argc < 4) || (std::string(argv[1]) != "-o"))
		error("syntax: multilink -o <outfile> <infiles...>");

	auto outfile = std::string(argv[2]);
	std::stringstream ss;
	for (int i=3; i<argc; i++)
	{
		ss << argv[i];
		ss << ' ';
	}
	auto infiles = ss.str();

	auto corefile = outfile + ".core";
	auto zpfile = outfile + ".zp";
	auto memfile = outfile + ".mem";

	if (system(fmt::format("ld65 -C scripts/link.cfg {} -o {}", infiles, corefile).c_str()) != 0)
		error("error: assembly failed");
	if (system(fmt::format("ld65 -C scripts/linkz.cfg {} -o {}", infiles, zpfile).c_str()) != 0)
		error("error: assembly failed (pass 2)");
	if (system(fmt::format("ld65 -C scripts/linkm.cfg {} -o {}", infiles, memfile).c_str()) != 0)
		error("error: assembly failed (pass 3)");

	auto coreSize = std::filesystem::file_size(corefile);

	auto zpDifferences = compare(corefile, zpfile);
	auto zpBytes = toBytestream(zpDifferences);
	auto memDifferences = compare(corefile, memfile);
	auto memBytes = toBytestream(memDifferences);

	std::ofstream outs(outfile);

	/* Write the header. */

	outs.write("CPM65", 5);
	emitw(outs, paras(coreSize));
	emitw(outs, paras(zpBytes.size()));
	emitw(outs, paras(memBytes.size()));
	align(outs);

	/* Write the actual code body. */

	{
		auto memi = memDifferences.begin();
		std::ifstream is(corefile);
		unsigned pos = 0;
		while (!is.eof())
		{
			uint8_t b = is.get();
			if (pos == *memi)
			{
				b -= 2;
				memi++;
			}
			outs.put(b);
			pos++;
		}
		align(outs);
	}

	/* Write the zero page relocation bytes. */

	for (uint8_t b : zpBytes)
		outs.put(b);
	align(outs);

	/* Write out the memory relocation bytes. */

	for (uint8_t b : memBytes)
		outs.put(b);
	align(outs);

	return 0;
}

