// This file is part of Notepad4.
// See License.txt for details about distribution and modification.
//! Lexer for SAS.

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
#include "LexerModule.h"

using namespace Lexilla;

namespace {

//KeywordIndex++Autogenerated -- start of section automatically generated
enum {
	KeywordIndex_Keyword = 0,
	KeywordIndex_Macro = 1,
	KeywordIndex_Function = 2,
};
//KeywordIndex--Autogenerated -- end of section automatically generated

void ColouriseSASDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	int visibleChars = 0;
	int indentCount = 0;
	int lineState = 0;
	int chPrevNonWhite = 0;

	StyleContext sc(startPos, lengthDoc, initStyle, styler);

	while (sc.More()) {
		switch (sc.state) {
		case SCE_SAS_OPERATOR:
			sc.SetState(SCE_SAS_DEFAULT);
			break;

		case SCE_SAS_NUMBER:
			if (!IsDecimalNumber(sc.chPrev, sc.ch, sc.chNext)) {
				sc.SetState(SCE_SAS_DEFAULT);
			}
			break;

		case SCE_SAS_MACRO:
		case SCE_SAS_IDENTIFIER:
			if (!IsIdentifierChar(sc.ch)) {
				char s[64];
				sc.GetCurrentLowered(s, sizeof(s));
				if (sc.state == SCE_SAS_MACRO) {
					if (keywordLists[KeywordIndex_Macro].InListPrefixed(s + 1, '(')) {
						sc.ChangeState((sc.ch == '(') ? SCE_SAS_MACRO_FUNCTION : SCE_SAS_MACRO_KEYWORD);
					}
				} else if (keywordLists[KeywordIndex_Keyword].InList(s)) {
					sc.ChangeState(SCE_SAS_WORD);
				} else if (sc.ch == '(' && keywordLists[KeywordIndex_Function].InListPrefixed(s, '(')) {
					sc.ChangeState(SCE_SAS_BASIC_FUNCTION);
				}
				sc.SetState(SCE_SAS_DEFAULT);
			}
			break;

		case SCE_SAS_STRINGDQ:
		case SCE_SAS_STRINGSQ:
			if ((sc.state == SCE_SAS_STRINGDQ && sc.ch == '\"')
				|| (sc.state == SCE_SAS_STRINGSQ && sc.ch == '\'')) {
				sc.Forward();
				while (IsAlpha(sc.ch)) {
					sc.Forward(); // ignore constant type
				}
				sc.SetState(SCE_SAS_DEFAULT);
			}
			break;

		case SCE_SAS_COMMENT:
		case SCE_SAS_COMMENTBLOCK:
			if (sc.atLineStart) {
				lineState = PyLineStateMaskCommentLine;
			}
			if ((sc.state == SCE_SAS_COMMENT && sc.ch == ';') || (sc.state != SCE_SAS_COMMENT && sc.Match('*', '/'))) {
				if (sc.state != SCE_SAS_COMMENT) {
					sc.Forward();
				}
				sc.ForwardSetState(SCE_SAS_DEFAULT);
				if (lineState == PyLineStateMaskCommentLine && sc.GetLineNextChar() != '\0') {
					lineState = 0;
				}
			}
			break;
		}

		if (sc.state == SCE_SAS_DEFAULT) {
			if (sc.ch == '\"') {
				sc.SetState(SCE_SAS_STRINGDQ);
			} else if (sc.ch == '\'') {
				sc.SetState(SCE_SAS_STRINGSQ);
			} else if (sc.Match('/', '*')) {
				sc.SetState(SCE_SAS_COMMENTBLOCK);
				sc.Forward();
			} else if (sc.Match('%', '*')) { // comment in macro
				sc.SetState(SCE_SAS_COMMENT);
			} else if (IsNumberStart(sc.ch, sc.chNext)) {
				sc.SetState(SCE_SAS_NUMBER);
			} else if (IsIdentifierStart(sc.ch) || (sc.ch == '%' && IsIdentifierStart(sc.chNext))) {
				sc.SetState((sc.ch == '%') ? SCE_SAS_MACRO : SCE_SAS_IDENTIFIER);
			} else if (IsAGraphic(sc.ch)) {
				sc.SetState(SCE_SAS_OPERATOR);
				if (sc.ch == '*' && AnyOf(chPrevNonWhite, 0, ';', '/')) {
					// * ... ; at line start, after statement or comment block
					sc.ChangeState(SCE_SAS_COMMENT);
				} else if (visibleChars == 0 && (sc.ch == '}' || sc.ch == ']' || sc.ch == ')')) {
					lineState |= PyLineStateMaskCloseBrace;
				}
			}
			if (visibleChars == 0 && (sc.state == SCE_SAS_COMMENT || sc.state == SCE_SAS_COMMENTBLOCK)) {
				lineState = PyLineStateMaskCommentLine;
			}
		}

		if (visibleChars == 0) {
			if (sc.ch == ' ') {
				++indentCount;
			} else if (sc.ch == '\t') {
				indentCount = GetTabIndentCount(indentCount);
			} else if (!isspacechar(sc.ch)) {
				visibleChars++;
			}
		}
		if (sc.ch > ' ') {
			chPrevNonWhite = sc.ch;
		}
		if (sc.atLineEnd) {
			lineState |= (indentCount << 16);
			if (visibleChars == 0 && (lineState & PyLineStateMaskCommentLine) == 0) {
				lineState |= PyLineStateMaskEmptyLine;
			}
			styler.SetLineState(sc.currentLine, lineState);
			lineState = 0;
			visibleChars = 0;
			indentCount = 0;
			chPrevNonWhite = 0;
		}
		sc.Forward();
	}

	sc.Complete();
}

}

extern const LexerModule lmSAS(SCLEX_SAS, ColouriseSASDoc, "sas", FoldPyDoc);
