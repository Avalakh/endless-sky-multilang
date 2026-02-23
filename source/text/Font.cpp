/* Font.cpp
Copyright (c) 2014-2020 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "Font.h"

#include "Alignment.h"
#include "Utf8.h"
#include "../Color.h"
#include "DisplayText.h"
#include "../Files.h"
#include "../GameData.h"
#include "../image/ImageBuffer.h"
#include "../image/ImageFileData.h"
#include "../Point.h"
#include "../Preferences.h"
#include "../Screen.h"
#include "Truncate.h"
#include "Utf8.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace std;

namespace {
	bool showUnderlines = false;

	enum class VerticalPlacement {
		BOTTOM,
		MIDDLE,
		TOP
	};

	VerticalPlacement GlyphVerticalPlacement(uint32_t codepoint)
	{
		switch(codepoint)
		{
			// Midline punctuation / operators / dashes.
			case 0x002Bu: // +
			case 0x003Du: // =
			case 0x002Au: // *
			case 0x002Fu: // /
			case 0x005Cu: // '\'
			case 0x007Cu: // |
			case 0x003Cu: // <
			case 0x003Eu: // >
			case 0x007Eu: // ~
			case 0x002Du: // -
			case 0x2010u: // ‐
			case 0x2011u: // ‑
			case 0x2012u: // ‒
			case 0x2013u: // –
			case 0x2014u: // —
			case 0x00ABu: // «
			case 0x00BBu: // »
			case 0x2039u: // ‹
			case 0x203Au: // ›
			case 0x00B7u: // ·
			case 0x2022u: // •
			case 0x2219u: // ∙
				return VerticalPlacement::MIDDLE;

			// Top punctuation / quote-like / accent marks.
			case 0x0027u: // '
			case 0x0022u: // "
			case 0x0060u: // `
			case 0x005Eu: // ^
			case 0x00B4u: // ´
			case 0x00A8u: // ¨
			case 0x00AFu: // ¯
			case 0x02BCu: // ʼ
			case 0x02C7u: // ˇ
			case 0x02CAu: // ˊ
			case 0x02CBu: // ˋ
			case 0x02DCu: // ˜
			case 0x2018u: // ‘
			case 0x2019u: // ’
			case 0x201Au: // ‚
			case 0x201Bu: // ‛
			case 0x201Cu: // “
			case 0x201Du: // ”
			case 0x201Eu: // „
			case 0x201Fu: // ‟
			case 0x2032u: // ′
			case 0x2033u: // ″
				return VerticalPlacement::TOP;

			// Default: baseline/bottom-aligned symbols (letters, digits, . , _ etc).
			default:
				return VerticalPlacement::BOTTOM;
		}
	}

	/// Returns a substitute codepoint for unsupported special characters, or 0 if none.
	/// Only maps symbols where substitute preserves meaning; otherwise caller uses space.
	uint32_t SubstituteUnsupported(uint32_t codepoint)
	{
		switch(codepoint)
		{
			case 0x00A0u: return 0x0020u; // non-breaking space → space
			case 0x2009u: return 0x0020u; // thin space → space
			case 0x2010u: case 0x2011u: case 0x2012u: case 0x2013u: return 0x002Du; // hyphens/dashes → -
			case 0x2019u: case 0x201Au: case 0x201Bu: return 0x0027u; // apostrophe/single quotes → '
			case 0x201Du: case 0x201Eu: case 0x201Fu: return 0x0022u; // right/other double quotes → "
			case 0x2026u: case 0x2033u: case 0x2036u: return 0x0020u; // ellipsis, prime symbols → space (meaning lost)
			default: return 0;
		}
	}

	/// Shared VAO and VBO quad (0,0) -> (1,1)
	GLuint vao = 0;
	GLuint vbo = 0;

	GLint colorI = 0;
	GLint scaleI = 0;
	GLint glyphSizeI = 0;
	GLint glyphI = 0;
	GLint aspectI = 0;
	GLint positionI = 0;
	GLint glyphCountI = 0;

	GLint vertI;
	GLint cornerI;

	void EnableAttribArrays()
	{
		// Connect the xy to the "vert" attribute of the vertex shader.
		constexpr auto stride = 4 * sizeof(GLfloat);
		glEnableVertexAttribArray(vertI);
		glVertexAttribPointer(vertI, 2, GL_FLOAT, GL_FALSE, stride, nullptr);

		glEnableVertexAttribArray(cornerI);
		glVertexAttribPointer(cornerI, 2, GL_FLOAT, GL_FALSE,
			stride, reinterpret_cast<const GLvoid *>(2 * sizeof(GLfloat)));
	}

	// UTF-8 safe: byte offset after n code points (start of (n+1)-th), or str.size().
	size_t ByteOffsetAfterCodePoints(const string &str, int n)
	{
		size_t pos = 0;
		for(int i = 0; i < n && pos != string::npos && pos < str.size(); ++i)
			pos = Utf8::NextCodePoint(str, pos);
		return (pos == string::npos || pos > str.size()) ? str.size() : pos;
	}

	int CountCodePoints(const string &str)
	{
		int count = 0;
		size_t pos = 0;
		while(pos < str.size() && pos != string::npos)
		{
			++count;
			pos = Utf8::NextCodePoint(str, pos);
		}
		return count;
	}

	string SubstringFirstCodePoints(const string &str, int n)
	{
		return str.substr(0, ByteOffsetAfterCodePoints(str, n));
	}

	string SubstringLastCodePoints(const string &str, int n)
	{
		const int total = CountCodePoints(str);
		if(total <= n)
			return str;
		return str.substr(ByteOffsetAfterCodePoints(str, total - n));
	}
}



Font::Font(const filesystem::path &imagePath)
{
	Load(imagePath);
}



void Font::Load(const filesystem::path &imagePath)
{
	ImageBuffer image;
	if(!image.Read(ImageFileData(imagePath)))
		return;

	glyphCount = GLYPHS;
	LoadTexture(image);
	CalculateAdvances(image, glyphCount);
	SetUpShader(static_cast<float>(image.Width()) / glyphCount, static_cast<float>(image.Height()), glyphCount);
	widthEllipses = WidthRawString(string("..."));
}


void Font::LoadFromTtf(const filesystem::path &ttfPath, int pixelHeight)
{
	if(texture)
	{
		glDeleteTextures(1, &texture);
		texture = 0;
	}

	string ttfData = Files::Read(ttfPath);
	if(ttfData.empty())
		return;

	const unsigned char *data = reinterpret_cast<const unsigned char *>(ttfData.data());
	stbtt_fontinfo font;
	if(!stbtt_InitFont(&font, data, stbtt_GetFontOffsetForIndex(data, 0)))
		return;

	const float scale = stbtt_ScaleForPixelHeight(&font, static_cast<float>(pixelHeight));

	// Codepoints for 167 glyphs: ASCII 32-126 (95), then 96='?', 97=open quote, 98=close quote;
	// then А-Я (98-129), Ё(130), а-я(131-162), ё(163); then 164=em dash, 165=«, 166=».
	vector<uint32_t> codepoints;
	codepoints.reserve(GLYPHS_EXTENDED);
	for(int c = 32; c <= 126; ++c)
		codepoints.push_back(static_cast<uint32_t>(c));
	codepoints.push_back(63);
	codepoints.push_back(0x2018u);
	codepoints.push_back(0x201Cu);
	for(int c = 0x0410; c <= 0x042F; ++c)
		codepoints.push_back(static_cast<uint32_t>(c));
	codepoints.push_back(0x0401u);
	for(int c = 0x0430; c <= 0x044F; ++c)
		codepoints.push_back(static_cast<uint32_t>(c));
	codepoints.push_back(0x0451u);
	codepoints.push_back(0x2014u); // em dash —
	codepoints.push_back(0x00ABu); // left guillemet «
	codepoints.push_back(0x00BBu); // right guillemet »

	const int cellW = pixelHeight * 2;
	const int cellH = pixelHeight * 2;
	const int atlasW = GLYPHS_EXTENDED * cellW;
	const int atlasH = cellH;

	ImageBuffer image;
	image.Clear(1);
	image.Allocate(atlasW, atlasH);
	uint32_t *pixels = image.Pixels();
	fill(pixels, pixels + atlasW * atlasH, 0u);

	vector<unsigned char> temp(cellW * cellH);
	for(size_t i = 0; i < codepoints.size(); ++i)
	{
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		stbtt_GetCodepointBitmapBox(&font, codepoints[i], scale, scale, &x0, &y0, &x1, &y1);
		int w = x1 - x0;
		int h = y1 - y0;
		if(w <= 0 || h <= 0)
			continue;
		if(w > cellW) w = cellW;
		if(h > cellH) h = cellH;
		fill(temp.begin(), temp.end(), 0);
		stbtt_MakeCodepointBitmap(&font, temp.data(), w, h, cellW, scale, scale, codepoints[i]);
		int dx = i * cellW + (cellW - w) / 2;
		const int bottomAnchor = cellH;
		const int middleAnchor = bottomAnchor - cellH / 2;
		const int topAnchor = bottomAnchor - cellH;

		int dy = 0;
		switch(GlyphVerticalPlacement(codepoints[i]))
		{
			case VerticalPlacement::BOTTOM:
				dy = bottomAnchor - h;
				break;
			case VerticalPlacement::MIDDLE:
				dy = static_cast<int>(round(middleAnchor - h / 2.));
				break;
			case VerticalPlacement::TOP:
				dy = topAnchor;
				break;
		}
		// Additional global lift for Russian TTF text: raise all placements by one font height.
		dy -= cellH / 2;
		dy = max(0, min(cellH - h, dy));
		for(int y = 0; y < h; ++y)
			for(int x = 0; x < w; ++x)
			{
				unsigned a = temp[y * cellW + x];
				pixels[(dy + y) * atlasW + (dx + x)] = (static_cast<uint32_t>(a) << 24)
					| (static_cast<uint32_t>(a) << 16) | (static_cast<uint32_t>(a) << 8) | a;
			}
	}

	glyphCount = GLYPHS_EXTENDED;
	LoadTexture(image);
	CalculateAdvances(image, glyphCount);
	SetUpShader(static_cast<float>(cellW), static_cast<float>(cellH), glyphCount);
	widthEllipses = WidthRawString(string("..."));
}


void Font::Draw(const DisplayText &text, const Point &point, const Color &color) const
{
	DrawAliased(text, round(point.X()), round(point.Y()), color);
}



void Font::DrawAliased(const DisplayText &text, double x, double y, const Color &color) const
{
	int width = -1;
	const string truncText = TruncateText(text, width);
	const auto &layout = text.GetLayout();
	if(width >= 0)
	{
		if(layout.align == Alignment::CENTER)
			x += (layout.width - width) / 2;
		else if(layout.align == Alignment::RIGHT)
			x += layout.width - width;
	}
	DrawAliased(truncText, x, y, color);
}



void Font::Draw(const string &str, const Point &point, const Color &color) const
{
	DrawAliased(str, round(point.X()), round(point.Y()), color);
}



void Font::DrawAliased(const string &str, double x, double y, const Color &color) const
{
	glUseProgram(shader->Object());
	glUniform1i(glyphCountI, glyphCount);
	glBindTexture(GL_TEXTURE_2D, texture);
	if(OpenGL::HasVaoSupport())
		glBindVertexArray(vao);
	else
	{
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		EnableAttribArrays();
	}

	glUniform4fv(colorI, 1, color.Get());

	// Update the scale, only if the screen size has changed.
	if(Screen::Width() != screenWidth || Screen::Height() != screenHeight)
	{
		screenWidth = Screen::Width();
		screenHeight = Screen::Height();
		scale[0] = 2.f / screenWidth;
		scale[1] = -2.f / screenHeight;
	}
	const int kern = Preferences::LetterSpacing();
	const float scaleFactor = Preferences::FontScale() / 100.f;
	glUniform2fv(scaleI, 1, scale);
	glUniform2f(glyphSizeI, glyphWidth * scaleFactor, glyphHeight * scaleFactor);

	const double drawY = y;
	GLfloat textPos[2] = {
		static_cast<float>(x - 1.),
		static_cast<float>(drawY)};
	int previous = 0;
	bool isAfterSpace = true;
	bool underlineChar = false;
	const int underscoreGlyph = max(0, min(glyphCount - 1, static_cast<int>('_' - 32)));

	size_t pos = 0;
	while(pos < str.size())
	{
		char32_t cp = Utf8::DecodeCodePoint(str, pos);
		if(pos == string::npos)
			break;

		if(cp == '_')
		{
			underlineChar = showUnderlines;
			continue;
		}

		int glyph = GlyphForCodepoint(cp, isAfterSpace);
		if(cp != '"' && cp != '\'')
			isAfterSpace = !glyph;
		if(!glyph)
		{
			textPos[0] += space * scaleFactor;
			continue;
		}

		glUniform1i(glyphI, glyph);
		glUniform1f(aspectI, 1.f);

		textPos[0] += (advance[previous * glyphCount + glyph] + kern) * scaleFactor;
		glUniform2fv(positionI, 1, textPos);

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		if(underlineChar)
		{
			glUniform1i(glyphI, underscoreGlyph);
			glUniform1f(aspectI, static_cast<float>(advance[glyph * glyphCount] + kern)
				/ (advance[underscoreGlyph * glyphCount] + kern));

			glUniform2fv(positionI, 1, textPos);

			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			underlineChar = false;
		}

		previous = glyph;
	}

	if(OpenGL::HasVaoSupport())
		glBindVertexArray(0);
	else
	{
		glDisableVertexAttribArray(vertI);
		glDisableVertexAttribArray(cornerI);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
	}
	glUseProgram(0);
}



int Font::Width(const string &str, char after) const
{
	return WidthRawString(str, after);
}



int Font::FormattedWidth(const DisplayText &text, char after) const
{
	int width = -1;
	const string truncText = TruncateText(text, width);
	return width < 0 ? WidthRawString(truncText, after) : width;
}



int Font::Height() const noexcept
{
	return static_cast<int>(height * Preferences::FontScale() / 100.);
}



int Font::Space() const noexcept
{
	return static_cast<int>(space * Preferences::FontScale() / 100.);
}



void Font::ShowUnderlines(bool show) noexcept
{
	showUnderlines = show || Preferences::Has("Always underline shortcuts");
}



int Font::Glyph(char c, bool isAfterSpace) noexcept
{
	// Curly quotes.
	if(c == '\'' && isAfterSpace)
		return 96;
	if(c == '"' && isAfterSpace)
		return 97;

	return max(0, min(GLYPHS - 3, c - 32));
}



int Font::GlyphForCodepoint(uint32_t codepoint, bool isAfterSpace) const
{
	constexpr char32_t invalidCp = 0xFFFFFFFFu;
	// Curly quotes (ASCII and Unicode opening quotes).
	if((codepoint == '\'' || codepoint == 0x2018u) && isAfterSpace)
		return 96;
	if((codepoint == '"' || codepoint == 0x201Cu) && isAfterSpace)
		return 97;
	// ASCII printable range: use existing glyphs.
	if(codepoint >= 32 && codepoint <= 126)
		return max(0, min(glyphCount - 3, static_cast<int>(codepoint - 32)));
	if(glyphCount == GLYPHS_EXTENDED)
	{
		if(codepoint >= 0x0410u && codepoint <= 0x042Fu)
			return 98 + static_cast<int>(codepoint - 0x0410u);
		if(codepoint == 0x0401u) return 130;
		if(codepoint >= 0x0430u && codepoint <= 0x044Fu)
			return 131 + static_cast<int>(codepoint - 0x0430u);
		if(codepoint == 0x0451u) return 163;
		if(codepoint == 0x2014u) return 164; // em dash —
		if(codepoint == 0x00ABu) return 165; // left guillemet «
		if(codepoint == 0x00BBu) return 166; // right guillemet »
	}
	// Fallback: try substitute for other unsupported special characters.
	if(codepoint != invalidCp)
	{
		uint32_t sub = SubstituteUnsupported(codepoint);
		if(sub)
			return GlyphForCodepoint(sub, isAfterSpace);
		// No suitable substitute: output space to avoid misleading glyph (?).
		return 0;
	}
	return 0;
}



void Font::LoadTexture(ImageBuffer &image)
{
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.Width(), image.Height(), 0,
		GL_RGBA, GL_UNSIGNED_BYTE, image.Pixels());
}



void Font::CalculateAdvances(ImageBuffer &image, int glyphCountParam)
{
	int width = image.Width() / glyphCountParam;
	height = image.Height();
	unsigned mask = 0xFF000000;
	unsigned half = 0xC0000000;
	int pitch = image.Width();

	advance.resize(glyphCountParam * glyphCountParam);
	fill(advance.begin(), advance.end(), 0);
	for(int previous = 1; previous < glyphCountParam; ++previous)
		for(int next = 0; next < glyphCountParam; ++next)
		{
			int maxD = 0;
			int glyphWidth = 0;
			uint32_t *begin = image.Pixels();
			for(int y = 0; y < height; ++y)
			{
				// Find the last non-empty pixel in the previous glyph.
				uint32_t *pend = begin + previous * width;
				uint32_t *pit = pend + width;
				while(pit != pend && (*--pit & mask) < half) {}
				int distance = (pit - pend) + 1;
				glyphWidth = max(distance, glyphWidth);

				// Special case: if "next" is zero (i.e. end of line of text),
				// calculate the full width of this character. Otherwise:
				if(next)
				{
					// Find the first non-empty pixel in this glyph.
					uint32_t *nit = begin + next * width;
					uint32_t *nend = nit + width;
					while(nit != nend && (*nit++ & mask) < half) {}

					// How far apart do you want these glyphs drawn? If drawn at
					// an advance of "width", there would be:
					// pend + width - pit   <- pixels after the previous glyph.
					// nit - (nend - width) <- pixels before the next glyph.
					// So for zero kerning distance, you would want:
					distance += 1 - (nit - (nend - width));
				}
				maxD = max(maxD, distance);

				// Update the pointer to point to the beginning of the next row.
				begin += pitch;
			}
			// This is a fudge factor to avoid over-kerning, especially for the
			// underscore and for glyph combinations like AV.
			advance[previous * glyphCountParam + next] = max(maxD, glyphWidth - 4) / 2;
		}

	// Set the space size based on the character width.
	width /= 2;
	height /= 2;
	space = (width + 3) / 6 + 1;
}



void Font::SetUpShader(float glyphW, float glyphH, int glyphCountParam)
{
	glyphWidth = glyphW * .5f;
	glyphHeight = glyphH * .5f;

	shader = GameData::Shaders().Get("font");
	if(!vbo)
	{
		vertI = shader->Attrib("vert");
		cornerI = shader->Attrib("corner");

		glUseProgram(shader->Object());
		glUniform1i(shader->Uniform("tex"), 0);
		glUseProgram(0);

		// Create the VAO and VBO.
		if(OpenGL::HasVaoSupport())
		{
			glGenVertexArrays(1, &vao);
			glBindVertexArray(vao);
		}

		glGenBuffers(1, &vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);

		GLfloat vertices[] = {
				0.f, 0.f, 0.f, 0.f,
				0.f, 1.f, 0.f, 1.f,
				1.f, 0.f, 1.f, 0.f,
				1.f, 1.f, 1.f, 1.f
		};
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

		if(OpenGL::HasVaoSupport())
			EnableAttribArrays();

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		if(OpenGL::HasVaoSupport())
			glBindVertexArray(0);

		colorI = shader->Uniform("color");
		scaleI = shader->Uniform("scale");
		glyphSizeI = shader->Uniform("glyphSize");
		glyphI = shader->Uniform("glyph");
		aspectI = shader->Uniform("aspect");
		positionI = shader->Uniform("position");
		glyphCountI = shader->Uniform("glyphCount");
	}

	screenWidth = 0;
	screenHeight = 0;
}



int Font::WidthRawString(const string &str, char after) const noexcept
{
	int width = 0;
	int previous = 0;
	bool isAfterSpace = true;
	const int kern = Preferences::LetterSpacing();

	size_t pos = 0;
	while(pos < str.size())
	{
		char32_t cp = Utf8::DecodeCodePoint(str, pos);
		if(pos == string::npos)
			break;

		if(cp == '_')
			continue;

		int glyph = GlyphForCodepoint(cp, isAfterSpace);
		if(cp != '"' && cp != '\'')
			isAfterSpace = !glyph;
		if(!glyph)
			width += space;
		else
		{
			width += advance[previous * glyphCount + glyph] + kern;
			previous = glyph;
		}
	}
	const int afterGlyph = (after >= 32 && after <= 126) ? (after - 32) : 0;
	width += advance[previous * glyphCount + max(0, min(glyphCount - 1, afterGlyph))];

	return static_cast<int>(width * Preferences::FontScale() / 100.);
}



// Param width will be set to the width of the return value, unless the layout width is negative.
string Font::TruncateText(const DisplayText &text, int &width) const
{
	width = -1;
	const auto &layout = text.GetLayout();
	const string &str = text.GetText();
	if(layout.width < 0 || (layout.align == Alignment::LEFT && layout.truncate == Truncate::NONE))
		return str;
	width = layout.width;
	switch(layout.truncate)
	{
		case Truncate::NONE:
			width = WidthRawString(str);
			return str;
		case Truncate::FRONT:
			return TruncateFront(str, width);
		case Truncate::MIDDLE:
			return TruncateMiddle(str, width);
		case Truncate::BACK:
		default:
			return TruncateBack(str, width);
	}
}



string Font::TruncateBack(const string &str, int &width) const
{
	return TruncateEndsOrMiddle(str, width,
		[](const string &str, int charCount)
		{
			return SubstringFirstCodePoints(str, charCount) + "...";
		});
}



string Font::TruncateFront(const string &str, int &width) const
{
	return TruncateEndsOrMiddle(str, width,
		[](const string &str, int charCount)
		{
			return "..." + SubstringLastCodePoints(str, charCount);
		});
}



string Font::TruncateMiddle(const string &str, int &width) const
{
	return TruncateEndsOrMiddle(str, width,
		[](const string &str, int charCount)
		{
			return SubstringFirstCodePoints(str, (charCount + 1) / 2) + "..."
				+ SubstringLastCodePoints(str, charCount / 2);
		});
}



string Font::TruncateEndsOrMiddle(const string &str, int &width,
	function<string(const string &, int)> getResultString) const
{
	int firstWidth = WidthRawString(str);
	if(firstWidth <= width)
	{
		width = firstWidth;
		return str;
	}

	int workingChars = 0;
	int workingWidth = 0;

	const int codePointCount = CountCodePoints(str);
	int low = 0, high = codePointCount;
	while(low <= high)
	{
		// Think "how many chars to take from both ends, omitting in the middle".
		int nextChars = (low + high) / 2;
		int nextWidth = WidthRawString(getResultString(str, nextChars));
		if(nextWidth <= width)
		{
			if(nextChars > workingChars)
			{
				workingChars = nextChars;
				workingWidth = nextWidth;
			}
			low = nextChars + (nextChars == low);
		}
		else
			high = nextChars - 1;
	}
	width = workingWidth;
	return getResultString(str, workingChars);
}
