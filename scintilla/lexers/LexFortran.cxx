// This file is part of Notepad4.
// See License.txt for details about distribution and modification.
//! Lexer for Fortran

#include <cassert>
#include <cstring>

#include <string>
#include <string_view>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "StringUtils.h"
#include "LexerModule.h"

using namespace Lexilla;

namespace {

struct EscapeSequence {
	int outerState = SCE_F_DEFAULT;
	int digitsLeft = 0;

	bool resetEscapeState(int state, int chNext) noexcept {
		outerState = state;
		digitsLeft = 0;
		if (chNext == 'x') {
			digitsLeft = 3;
		} else if (chNext == 'u') {
			digitsLeft = 5;
		} else if (chNext == 'U') {
			digitsLeft = 9;
		} else if (AnyOf(chNext, '\\', '\'', '\"', 'a', 'b', 'f', 'n', 'r', 't', 'v', '0')) {
			digitsLeft = 1;
		}
		return digitsLeft != 0;
	}
	bool atEscapeEnd(int ch) noexcept {
		--digitsLeft;
		return digitsLeft <= 0 || !IsHexDigit(ch);
	}
};

enum {
	FortranLineStateMaskLineComment = 1,
	FortranLineStateLineContinuation = 1 << 1,
};

//KeywordIndex++Autogenerated -- start of section automatically generated
enum {
	KeywordIndex_Keyword = 0,
	KeywordIndex_CodeFolding = 1,
	KeywordIndex_Type = 2,
	KeywordIndex_Attribute = 3,
	KeywordIndex_Function = 4,
};
//KeywordIndex--Autogenerated -- end of section automatically generated

enum class KeywordType {
	None = 0,
	Change,		// change team
	Select,		// select type, enumeration type
	Else,		// else if
	Module,		// module function, module subroutine
	End,
	Type,
	Function = SCE_F_FUNCTION_DEFINITION,
	Call = SCE_F_FUNCTION,
};

void ColouriseFortranDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	int lineState = 0;
	int visibleChars = 0;
	int parenCount = 0;
	bool ifConstruct = false;
	KeywordType kwType = KeywordType::None;
	EscapeSequence escSeq;

	StyleContext sc(startPos, lengthDoc, initStyle, styler);
	if (sc.currentLine > 0) {
		lineState = styler.GetLineState(sc.currentLine - 1);
		/*
		8: lineState
		: parenCount
		*/
		parenCount = lineState >> 8;
		lineState &= FortranLineStateLineContinuation;
	}

	while (sc.More()) {
		switch (sc.state) {
		case SCE_F_OPERATOR:
			sc.SetState(SCE_F_DEFAULT);
			break;

		case SCE_F_OPERATOR2:
			if (sc.ch == '.') {
				sc.ForwardSetState(SCE_F_DEFAULT);
			} else if (!IsAlpha(sc.ch)) {
				// . alpha .
				sc.Rewind();
				sc.SetState(SCE_F_OPERATOR);
			}
			break;

		case SCE_F_NUMBER:
			if (!IsIdentifierChar(sc.ch)) {
				if (IsFloatExponent(sc.chPrev, sc.ch, sc.chNext)) {
					sc.Forward();
				} else if (sc.ch != '.' || sc.chPrev == '.' || (IsAlpha(sc.chNext)
					&& !(UnsafeLower(sc.chNext) == 'e' && IsADigit(sc.GetRelative(2))))) {
					sc.SetState(SCE_F_DEFAULT);
				}
			}
			break;

		case SCE_F_IDENTIFIER:
			if (!IsIdentifierChar(sc.ch)) {
				char s[64];
				sc.GetCurrentLowered(s, sizeof(s));

				const KeywordType prevWord = kwType;
				kwType = KeywordType::None;
				int state = SCE_F_IDENTIFIER;

				if (keywordLists[KeywordIndex_CodeFolding].InList(s)) {
					state = SCE_F_FOLDING_WORD;
				} else if (keywordLists[KeywordIndex_Keyword].InList(s)) {
					state = SCE_F_WORD;
				} else if (keywordLists[KeywordIndex_Type].InList(s)) {
					state = SCE_F_TYPE;
				} else if (keywordLists[KeywordIndex_Attribute].InList(s)) {
					state = SCE_F_ATTRIBUTE;
				} else if (keywordLists[KeywordIndex_Function].InListPrefixed(s, '(')) {
					state = SCE_F_INTRINSIC;
				} else if (prevWord == KeywordType::Function || prevWord == KeywordType::Call) {
					state = static_cast<int>(prevWord);
				}
				if (state == SCE_F_WORD || state == SCE_F_FOLDING_WORD) {
					int chNext = sc.ch;
					Sci_PositionU pos = sc.currentPos;
					if (chNext <= ' ' && pos < sc.lineStartNext) {
						chNext = sc.chNext;
						++pos;
					}
					while (chNext <= ' ' && pos < sc.lineStartNext) {
						++pos;
						chNext = static_cast<uint8_t>(styler[pos]);
					}
					if (chNext == '=') {
						state = SCE_F_ATTRIBUTE;
					} else if (parenCount != 0) {
						state = SCE_F_WORD;
					} else if (state == SCE_F_WORD) {
						if (StrEqual(s, "call")) {
							kwType = KeywordType::Call;
						} if (StrEqual(s, "else")) {
							kwType = KeywordType::Else;
						} else if (StrEqual(s, "change")) {
							kwType = KeywordType::Change;
						} else if (ifConstruct && StrEqual(s, "then")) {
							ifConstruct = false;
							state = SCE_F_FOLDING_WORD;
						}
					} else {
						if (prevWord == KeywordType::End || prevWord == KeywordType::Module) {
							state = SCE_F_WORD;
						}
						if (StrEqual(s, "end")) {
							kwType = KeywordType::End;
						} else if (StrEqual(s, "module")) {
							kwType = KeywordType::Module;
						} else if (StrEqualsAny(s, "select", "enumeration")) {
							kwType = KeywordType::Select;
						} else if (StrEqualsAny(s, "function", "subroutine")) {
							kwType = KeywordType::Function;
						} else if (state == SCE_F_FOLDING_WORD) {
							if (StrEqual(s, "if")) {
								state = SCE_F_WORD;
								ifConstruct = prevWord != KeywordType::Else;
							} else if (StrEqual(s, "type")) {
								if (chNext == '(' || prevWord == KeywordType::Select) {
									state = SCE_F_WORD;
								} else {
									kwType = KeywordType::Type;
								}
							} else if (StrEqual(s, "team")) {
								if (prevWord != KeywordType::Change) {
									state = SCE_F_WORD;
								}
							}
						}
						if (state == SCE_F_FOLDING_WORD && (kwType == KeywordType::End || kwType == KeywordType::Type) && IsAlpha(chNext)) {
							char buf[12]{};
							styler.GetRangeLowered(pos, sc.lineStartNext, buf, sizeof(buf));
							size_t index = 0;
							if (kwType == KeywordType::Type) {
								// type is
								if (StrStartsWith(buf, "is")) {
									index = CStrLen("is");
								}
							} else {
								// end file
								// no code folding for gfortran: structure, union, map
								if (StrStartsWith(buf, "file")) {
									index = CStrLen("file");
								} else if (StrStartsWith(buf, "union")) {
									index = CStrLen("union");
								} else if (StrStartsWith(buf, "map")) {
									index = CStrLen("map");
								} else if (StrStartsWith(buf, "structure")) {
									index = CStrLen("structure");
								}
							}
							if (index != 0 && !IsIdentifierChar(buf[index])) {
								state = SCE_F_WORD;
							}
						}
					}
				}
				if (state != SCE_F_IDENTIFIER) {
					sc.ChangeState(state);
				}
				sc.SetState(SCE_F_DEFAULT);
			}
			break;

		case SCE_F_STRING_SQ:
		case SCE_F_STRING_DQ:
			if (sc.atLineStart) {
				if (lineState == FortranLineStateLineContinuation) {
					lineState = 0;
				} else {
					sc.SetState(SCE_F_DEFAULT);
					break;
				}
			}
			if (sc.ch == '&' && IsEOLChar(sc.chNext)) {
				lineState = FortranLineStateLineContinuation;
			} else if (sc.ch == '\\') {
				if (escSeq.resetEscapeState(sc.state, sc.chNext)) {
					sc.SetState(SCE_F_ESCAPECHAR);
					sc.Forward();
				}
			} else if (sc.ch == ((sc.state == SCE_F_STRING_SQ) ? '\'' : '\"')) {
				if (sc.ch == sc.chNext) {
					escSeq.outerState = sc.state;
					escSeq.digitsLeft = 1;
					sc.SetState(SCE_F_ESCAPECHAR);
				}
				sc.Forward();
				if (sc.state != SCE_F_ESCAPECHAR) {
					sc.SetState(SCE_F_DEFAULT);
				}
			}
			break;

		case SCE_F_ESCAPECHAR:
			if (escSeq.atEscapeEnd(sc.ch)) {
				sc.SetState(escSeq.outerState);
				continue;
			}
			break;

		case SCE_F_COMMENT:
		case SCE_F_PREPROCESSOR:
			if (sc.atLineStart) {
				sc.SetState(SCE_F_DEFAULT);
			}
			break;
		}

		if (sc.state == SCE_F_DEFAULT) {
			if (visibleChars == 0 && AnyOf(sc.ch, 'C', 'c', '*', '!')) {
				bool preprocessor = sc.chNext == '$';
				if (!preprocessor && IsAlpha(sc.chNext)) {
					char s[5];
					styler.GetRangeLowered(sc.currentPos + 1, sc.lineStartNext, s, sizeof(s));
					preprocessor = StrStartsWith(s, "dec$") || StrStartsWith(s, "dir$") || StrStartsWith(s, "gcc$") || StrStartsWith(s, "ms$");
				}
				if (preprocessor) {
					sc.SetState(SCE_F_PREPROCESSOR);
				} else if (UnsafeLower(sc.ch) == 'c' && IsAGraphic(sc.chNext)) {
					sc.SetState(SCE_F_IDENTIFIER);
				} else {
					lineState = SimpleLineStateMaskLineComment;
					sc.SetState(SCE_F_COMMENT);
				}
			} else if (visibleChars == 0 && sc.ch == '#') {
				sc.SetState(SCE_F_PREPROCESSOR);
			} else if (sc.ch == '!') {
				sc.SetState(SCE_F_COMMENT);
			} else if (sc.ch == '\"') {
				sc.SetState(SCE_F_STRING_DQ);
			} else if (sc.ch == '\'') {
				sc.SetState(SCE_F_STRING_SQ);
			} else if (IsNumberStart(sc.ch, sc.chNext)) {
				sc.SetState(SCE_F_NUMBER);
			} else if (IsIdentifierStart(sc.ch)) {
				sc.SetState(SCE_F_IDENTIFIER);
			} else if (sc.ch == '.' && IsAlpha(sc.chNext)) {
				sc.SetState(SCE_F_OPERATOR2);
			} else if (IsAGraphic(sc.ch)) {
				kwType = KeywordType::None;
				if (sc.ch == '(' || sc.ch == '[' || sc.ch == '{') {
					++parenCount;
				} else if (sc.ch == ')' || sc.ch == ']' || sc.ch == '}') {
					if (parenCount > 0) {
						--parenCount;
					}
				}
				sc.SetState(SCE_F_OPERATOR);
			}
		}

		if (visibleChars == 0 && !isspacechar(sc.ch)) {
			visibleChars++;
		}
		if (sc.atLineEnd) {
			styler.SetLineState(sc.currentLine, lineState | (parenCount << 8));
			lineState &= FortranLineStateLineContinuation;
			visibleChars = 0;
			kwType = KeywordType::None;
			ifConstruct = false;
		}
		sc.Forward();
	}

	sc.Complete();
}

constexpr int GetLineCommentState(int lineState) noexcept {
	return lineState & FortranLineStateMaskLineComment;
}

void FoldFortranDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int /*initStyle*/, LexerWordList /*keywordLists*/, Accessor &styler) {
	const Sci_PositionU endPos = startPos + lengthDoc;
	Sci_Line lineCurrent = styler.GetLine(startPos);
	int levelCurrent = SC_FOLDLEVELBASE;
	int lineCommentPrev = 0;
	if (lineCurrent > 0) {
		levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
		lineCommentPrev = GetLineCommentState(styler.GetLineState(lineCurrent - 1));
	}

	int levelNext = levelCurrent;
	int lineCommentCurrent = GetLineCommentState(styler.GetLineState(lineCurrent));
	Sci_PositionU lineStartNext = styler.LineStart(lineCurrent + 1);
	lineStartNext = sci::min(lineStartNext, endPos);

	int style = SCE_F_DEFAULT;
	while (startPos < endPos) {
		const int stylePrev = style;
		style = styler.StyleAt(startPos);

		if (style == SCE_F_FOLDING_WORD && stylePrev != SCE_F_FOLDING_WORD) {
			levelNext++;
			char ch = styler[startPos++];
			if (AnyOf<'C', 'E', 'c', 'e'>(ch)) {
				char buf[10] = {UnsafeLower(ch)}; // continue
				constexpr int MaxFoldWordLength = sizeof(buf) - 1;
				int wordLen = 1;
				while (wordLen < MaxFoldWordLength) {
					ch = styler[startPos];
					if (!IsAlpha(ch)) {
						break;
					}
					buf[wordLen] = UnsafeLower(ch);
					startPos++;
					wordLen++;
				}

				startPos--;
				buf[wordLen] = '\0';
				if (StrStartsWith(buf, "end") || StrEqual(buf, "continue")) {
					levelNext -= 2;
				}
			}
		}

		if (++startPos == lineStartNext) {
			const int lineCommentNext = GetLineCommentState(styler.GetLineState(lineCurrent + 1));
			levelNext = sci::max(levelNext, SC_FOLDLEVELBASE);
			if (lineCommentCurrent) {
				levelNext += lineCommentNext - lineCommentPrev;
			}

			const int levelUse = levelCurrent;
			int lev = levelUse | (levelNext << 16);
			if (levelUse < levelNext) {
				lev |= SC_FOLDLEVELHEADERFLAG;
			}
			styler.SetLevel(lineCurrent, lev);

			lineCurrent++;
			lineStartNext = styler.LineStart(lineCurrent + 1);
			lineStartNext = sci::min(lineStartNext, endPos);
			levelCurrent = levelNext;
			lineCommentPrev = lineCommentCurrent;
			lineCommentCurrent = lineCommentNext;
		}
	}
}

}

extern const LexerModule lmFortran(SCLEX_FORTRAN, ColouriseFortranDoc, "fortran", FoldFortranDoc);
