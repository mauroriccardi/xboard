/*
 * Engine-settings dialog. The complexity come from an attempt to present the engine-defined options
 * in a nicey formatted layout. To this end we first run a back-end pre-formatter, which will distribute
 * the controls over two columns (the minimum required, as some are double width). It also takes care of 
 * grouping options that start with the same word (mainly for "Polyglot ..." options). It assigns relative
 * suitability to break points between lines, and in the end decides if and where to break up the list
 * for display in multiple (2*N) columns.
 * The thus obtained list representing the topology of the layout is then passed to a front-end routine
 * that generates the actual dialog box from it.
 */

//#include "config.h"
#include "config.h"

#include <stdio.h>
#include "common.h"
#include "backend.h"

int layoutList[2*MAX_OPTIONS];
int checkList[2*MAX_OPTIONS];
int comboList[2*MAX_OPTIONS];
int buttonList[2*MAX_OPTIONS];
int boxList[2*MAX_OPTIONS];
int groupNameList[2*MAX_OPTIONS];
int breaks[MAX_OPTIONS];
int checks, combos, buttons, layout, groups;

void
PrintOpt(int i, int right, ChessProgramState *cps)
{
    if(i<0) {
	if(!right) fprintf(debugFP, "%30s", "");
    } else {
	Option opt = cps->option[i];
	switch(opt.type) {
	    case Spin:
		fprintf(debugFP, "%20.20s [    +/-]", opt.name);
		break;
	    case TextBox:
		fprintf(debugFP, "%20.20s [______________________________________]", opt.name);
		break;
	    case CheckBox:
		fprintf(debugFP, "[x] %-26.25s", opt.name);
		break;
	    case ComboBox:
		fprintf(debugFP, "%20.20s [ COMBO ]", opt.name);
		break;
	    case Button:
	    case SaveButton:
		fprintf(debugFP, "[ %26.26s ]", opt.name);
		break;
	}
    }
    fprintf(debugFP, right ? "\n" : " ");
}

void
CreateOptionDialogTest(int *list, int nr, ChessProgramState *cps)
{
    int line;

    for(line = 0; line < nr; line+=2) {
	PrintOpt(list[line+1], 0, cps);
	PrintOpt(list[line], 1, cps);
    }
}

void
LayoutOptions(int firstOption, int endOption, char *groupName, Option *optionList)
{
    int n, i, b = strlen(groupName), stop, prefix, right, nextOption, firstButton = buttons;
    Control lastType, nextType;

    nextOption = firstOption;
    while(nextOption < endOption) {
	checks = combos = 0; stop = 0;
	lastType = Button; // kludge to make sure leading Spin will not be prefixed
	// first separate out buttons for later treatment, and collect consecutive checks and combos
	while(nextOption < endOption && !stop) {
	    switch(nextType = optionList[nextOption].type) {
		case CheckBox: checkList[checks++] = nextOption; lastType = CheckBox; break;
		case ComboBox: comboList[combos++] = nextOption; lastType = ComboBox; break;
		case SaveButton:
		case Button:  buttonList[buttons++] = nextOption; lastType = Button; break;
		case TextBox:
		case Spin: stop++;
	    }
	    nextOption++;
	}
	// We now must be at the end, or looking at a spin or textbox (in nextType)
	if(!stop) 
	    nextType = Button; // kudge to flush remaining checks and combos undistorted
	// Take a new line if a spin follows combos or checks, or when we encounter a textbox
	if((combos+checks || nextType == TextBox) && layout&1) {
	    layoutList[layout++] = -1;
	}
	// The last check or combo before a spin will be put on the same line as that spin (prefix)
	// and will thus not be grouped with other checks and combos
	prefix = -1;
	if(nextType == Spin && lastType != Button) {
	    if(lastType == CheckBox) prefix = checkList[--checks]; else
	    if(lastType == ComboBox) prefix = comboList[--combos];
	}
	// if a combo is followed by a textbox, it must stay at the end of the combo/checks list to appear
	// immediately above the textbox, so treat it as check. (A check would automatically be and remain there.)
	if(nextType == TextBox && lastType == ComboBox)
	    checkList[checks++] = comboList[--combos];
	// Now append the checks behind the (remaining) combos to treat them as one group
	for(i=0; i< checks; i++) 
	    comboList[combos++] = checkList[i];
	// emit the consecutive checks and combos in two columns
	right = combos/2; // rounded down if odd!
	for(i=0; i<right; i++) {
	    breaks[layout/2] = 2;
	    layoutList[layout++] = comboList[i];
	    layoutList[layout++] = comboList[i + right];
	}
	// An odd check or combo (which could belong to following textBox) will be put in the left column
	// If there was an even number of checks and combos the last one will automatically be in that position
	if(combos&1) {
	    layoutList[layout++] = -1;
	    layoutList[layout++] = comboList[2*right];
	}
	if(nextType == TextBox) {
	    // A textBox is double width, so must be left-adjusted, and the right column remains empty
	    breaks[layout/2] = lastType == Button ? 0 : 100;
	    layoutList[layout++] = -1;
	    layoutList[layout++] = nextOption - 1;
	} else if(nextType == Spin) {
	    // A spin will go in the next available position (right to left!). If it had to be prefixed with
	    // a check or combo, this next position must be to the right, and the prefix goes left to it.
	    layoutList[layout++] = nextOption - 1;
	    if(prefix >= 0) layoutList[layout++] = prefix;
	}
    }
    // take a new line if needed
    if(layout&1) layoutList[layout++] = -1;
    // emit the buttons belonging in this group; loose buttons are saved for last, to appear at bottom of dialog
    if(b) {
	while(buttons > firstButton)
	    layoutList[layout++] = buttonList[--buttons];
	if(layout&1) layoutList[layout++] = -1;
    }
}

char *p
EndMatch(char *s1, char *s2)
{
	char *p, *q;
	p = s1; while(*p) p++;
	q = s2; while(*q) q++;
	while(p > s1 && q > s2 && *p == *q) { p--; q--; }
	if(p[1] == 0) return NULL;
	return p+1;
}

void
DesignOptionDialog(ChessProgramState *cps)
{
    int k=0, n=0;
    char buf[MSG_SIZ];

    layout = 0;
    buttons = groups = 0;
    while(k < cps->nrOptions) { // k steps through 'solitary' options
	// look if we hit a group of options having names that start with the same word
	int groupSize = 1, groupNameLength = 50;
	sscanf(cps->option[k].name, "%s", &buf); // get first word of option name
	while(k + groupSize < cps->nrOptions &&
	      strstr(cps->option[k+groupSize].name, buf) == cps->option[k+groupSize].name) {
		int j;
		for(j=0; j<groupNameLength; j++) // determine common initial part of option names
		    if( cps->option[k].name[j] != cps->option[k+groupSize].name[j]) break;
		groupNameLength = j;
		groupSize++;
		
	}
	if(groupSize > 3) {
		// We found a group to terminates the current section
		LayoutOptions(n, k, "", cps->option); // flush all solitary options appearing before the group
		groupNameList[groups] = groupNameLength;
		boxList[groups++] = layout; // group start in even entries
		LayoutOptions(k, k+groupSize, buf, cps->option); // flush the group
		boxList[groups++] = layout; // group end in odd entries
		k = n = k + groupSize;
	} else {
		// try to recognize "two-column groups" based on option suffix
		int j = 1;
		while((p = EndMatch(cps->option[k].name, EndMatch(cps->option[k+2*j].name)) &&
		      (q = EndMatch(cps->option[k+1].name, EndMatch(cps->option[k+2*j+1].name)) ) j++
	} else k += groupSize; // small groups are grouped with the solitary options
    }
    if(n != k) LayoutOptions(n, k, "", cps->option); // flush remaining solitary options
    // decide if and where we break into two column pairs
    
    // Emit buttons and add OK and cancel
//    for(k=0; k<buttons; k++) layoutList[layout++] = buttonList[k];
    // Create the dialog window
    if(appData.debugMode) CreateOptionDialogTest(layoutList, layout, cps);
//    CreateOptionDialog(layoutList, layout, cps);
}

#include <windows.h>

extern HINSTANCE hInst;

typedef struct {
    DLGITEMTEMPLATE item;
    WORD code;
    WORD controlType;
    wchar_t d1, data;
    WORD creationData;
} Item;

struct {
    DLGTEMPLATE header;
    WORD menu;
    WORD winClass;
    wchar_t title[20];
    WORD pointSize;
    wchar_t fontName[14];
    Item control[MAX_OPTIONS];
} template = {
    { DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_SETFONT, 0, 0, 0, 0, 295, 300 },
    0x0000, 0x0000, L"Engine #1 Settings ", 8, L"MS Sans Serif"
};

ChessProgramState *activeCps;

void
SetOptionValues(HWND hDlg, ChessProgramState *cps)
// Put all current option values in controls, and write option names next to them
{
    HANDLE hwndCombo;
    int i, k;
    char **choices, title[MSG_SIZ], *name;

    for(i=0; i<layout+buttons; i++) {
	int j=layoutList[i];
	if(j == -2) SetDlgItemText( hDlg, 2000+2*i, ". . ." );
	if(j<0) continue;
	name = cps->option[j].name;
	if(strstr(name, "Polyglot ") == name) name += 9;
	SetDlgItemText( hDlg, 2000+2*i, name );
//if(appData.debugMode) fprintf(debugFP, "# %s = %d\n",cps->option[j].name, cps->option[j].value );
	switch(cps->option[j].type) {
	    case Spin:
		SetDlgItemInt( hDlg, 2001+2*i, cps->option[j].value, TRUE );
		break;
	    case TextBox:
		SetDlgItemText( hDlg, 2001+2*i, cps->option[j].textValue );
		break;
	    case CheckBox:
		CheckDlgButton( hDlg, 2000+2*i, cps->option[j].value != 0);
		break;
	    case ComboBox:
		choices = (char**) cps->option[j].textValue;
		hwndCombo = GetDlgItem(hDlg, 2001+2*i);
		SendMessage(hwndCombo, CB_RESETCONTENT, 0, 0);
		for(k=0; k<cps->option[j].max; k++) {
		    SendMessage(hwndCombo, CB_ADDSTRING, 0, (LPARAM) choices[k]);
		}
		SendMessage(hwndCombo, CB_SELECTSTRING, (WPARAM) -1, (LPARAM) choices[cps->option[j].value]);
		break;
	    case Button:
	    case SaveButton:
		break;
	}
    }
    SetDlgItemText( hDlg, IDOK, "OK" );
    SetDlgItemText( hDlg, IDCANCEL, "Cancel" );
    sprintf(title, "%s Engine Settings (%s)", cps->which, cps->tidy); 
    title[0] &= ~32; // capitalize
    SetWindowText( hDlg, title);
    for(i=0; i<groups; i+=2) { 
	int id, p; char buf[MSG_SIZ];
	id = k = boxList[i];
	if(layoutList[k] < 0) k++;
	if(layoutList[k] < 0) continue;
	for(p=0; p<groupNameList[i]; p++) buf[p] = cps->option[layoutList[k]].name[p];
	buf[p] = 0;
	SetDlgItemText( hDlg, 2000+2*(id+MAX_OPTIONS), buf );
    }
}


void
GetOptionValues(HWND hDlg, ChessProgramState *cps)
// read out all controls, and if value is altered, remember it and send it to the engine
{
    HANDLE hwndCombo;
    int i, k, new, changed;
    char **choices, newText[MSG_SIZ], buf[MSG_SIZ];
    BOOL success;

    for(i=0; i<layout; i++) {
	int j=layoutList[i];
	if(j<0) continue;
	SetDlgItemText( hDlg, 2000+2*i, cps->option[j].name );
	switch(cps->option[j].type) {
	    case Spin:
		new = GetDlgItemInt( hDlg, 2001+2*i, &success, TRUE );
		if(!success) break;
		if(new < cps->option[j].min) new = cps->option[j].min;
		if(new > cps->option[j].max) new = cps->option[j].max;
		changed = 2*(cps->option[j].value != new);
		cps->option[j].value = new;
		break;
	    case TextBox:
	    case FileName:
	    case PathName:
		success = GetDlgItemText( hDlg, 2001+2*i, newText, MSG_SIZ - strlen(cps->option[j].name) - 9 );
		if(!success) break;
		changed = strcmp(cps->option[j].textValue, newText) != 0;
		strcpy(cps->option[j].textValue, newText);
		break;
	    case CheckBox:
		new = IsDlgButtonChecked( hDlg, 2000+2*i );
		changed = 2*(cps->option[j].value != new);
		cps->option[j].value = new;
		break;
	    case ComboBox:
		choices = (char**) cps->option[j].textValue;
		hwndCombo = GetDlgItem(hDlg, 2001+2*i);
		success = GetDlgItemText( hDlg, 2001+2*i, newText, MSG_SIZ );
		if(!success) break;
		new = -1;
		for(k=0; k<cps->option[j].max; k++) {
		    if(!strcmp(choices[k], newText)) new = k;
		}
		changed = new >= 0 && (cps->option[j].value != new);
		if(changed) cps->option[j].value = new;
		break;
	    case Button:
		break; // are treated instantly, so they have been sent already
	}
	if(changed == 2) sprintf(buf, "option %s=%d\n", cps->option[j].name, new); else
	if(changed == 1) sprintf(buf, "option %s=%s\n", cps->option[j].name, newText);
	if(changed) SendToProgram(buf, cps);
    }
}

LRESULT CALLBACK SettingsProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    static int * lpIndexFRC;
    BOOL index_is_ok;
    char buf[MSG_SIZ];
    int i, j;

    switch( message )
    {
    case WM_INITDIALOG:

//        CenterWindow(hDlg, GetWindow(hDlg, GW_OWNER));
	SetOptionValues(hDlg, activeCps);

//        SetFocus(GetDlgItem(hDlg, IDC_NFG_Edit));

        break;

    case WM_COMMAND:
        switch( LOWORD(wParam) ) {
        case IDOK:
	    GetOptionValues(hDlg, activeCps);
            EndDialog( hDlg, 0 );
            return TRUE;

        case IDCANCEL:
            EndDialog( hDlg, 1 );   
            return TRUE;

	default:
	    // program-defined push buttons
	    i = LOWORD(wParam);
	    if( i>=2000 &&  i < 2000+2*(layout+buttons)) {
		j = layoutList[(i - 2000)/2];
		if(j == -2) {
		          char filter[] = 
				"All files\0*.*\0BIN Files\0*.bin\0LOG Files\0*.log\0INI Files\0*.ini\0\0";
/*
{ 
		              'A','l','l',' ','F','i','l','e','s', 0,
		              '*','.','*', 0,
		              'B','I','N',' ','F','i','l','e','s', 0,
		              '*','.','b','i','n', 0,
		              0 };
*/
		          OPENFILENAME ofn;

		          strcpy( buf, "" );

		          ZeroMemory( &ofn, sizeof(ofn) );

		          ofn.lStructSize = sizeof(ofn);
		          ofn.hwndOwner = hDlg;
		          ofn.hInstance = hInst;
		          ofn.lpstrFilter = filter;
		          ofn.lpstrFile = buf;
		          ofn.nMaxFile = sizeof(buf);
		          ofn.lpstrTitle = "Choose Book";
		          ofn.Flags = OFN_FILEMUSTEXIST | OFN_LONGNAMES | OFN_HIDEREADONLY;

		          if( GetOpenFileName( &ofn ) ) {
		              SetDlgItemText( hDlg, i+3, buf );
		          }
		}
		if(j < 0) break;
		if( activeCps->option[j].type  == SaveButton)
		     GetOptionValues(hDlg, activeCps);
		else if( activeCps->option[j].type  != Button) break;
		sprintf(buf, "option %s\n", activeCps->option[j].name);
		SendToProgram(buf, activeCps);
	    }
	    break;
        }

        break;
    }

    return FALSE;
}

#if 0
// example copied from MS docs
#define ID_HELP   150
#define ID_TEXT   200

LPWORD lpwAlign(LPWORD lpIn)
{
    ULONG ul;

    ul = (ULONG)lpIn;
    ul ++;
    ul >>=1;
    ul <<=1;
    return (LPWORD)ul;
}

LRESULT DisplayMyMessage(HINSTANCE hinst, HWND hwndOwner, LPSTR lpszMessage)
{
    HGLOBAL hgbl;
    LPDLGTEMPLATE lpdt;
    LPDLGITEMTEMPLATE lpdit;
    LPWORD lpw;
    LPWSTR lpwsz;
    LRESULT ret;
    int nchar;

    hgbl = GlobalAlloc(GMEM_ZEROINIT, 1024);
    if (!hgbl)
        return -1;
 
    lpdt = (LPDLGTEMPLATE)GlobalLock(hgbl);
 
    // Define a dialog box.
 
    lpdt->style = WS_POPUP | WS_BORDER | WS_SYSMENU | DS_MODALFRAME | WS_CAPTION;
//		  WS_POPUP |             WS_SYSMENU | DS_MODALFRAME | WS_CAPTION | DS_SETFONT
    lpdt->cdit = 3;         // Number of controls
    lpdt->x  = 10;  lpdt->y  = 10;
    lpdt->cx = 100; lpdt->cy = 100;

    lpw = (LPWORD)(lpdt + 1);
    *lpw++ = 0;             // No menu
    *lpw++ = 0;             // Predefined dialog box class (by default)

    lpwsz = (LPWSTR)lpw;
    nchar = 1 + MultiByteToWideChar(CP_ACP, 0, "My Dialog", -1, lpwsz, 50);
    lpw += nchar;

    //-----------------------
    // Define an OK button.
    //-----------------------
    lpw = lpwAlign(lpw);    // Align DLGITEMTEMPLATE on DWORD boundary
    lpdit = (LPDLGITEMTEMPLATE)lpw;
    lpdit->x  = 10; lpdit->y  = 70;
    lpdit->cx = 80; lpdit->cy = 20;
    lpdit->id = IDOK;       // OK button identifier
    lpdit->style = WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON;

    lpw = (LPWORD)(lpdit + 1);
    *lpw++ = 0xFFFF;
    *lpw++ = 0x0080;        // Button class

    lpwsz = (LPWSTR)lpw;
    nchar = 1 + MultiByteToWideChar(CP_ACP, 0, "OK", -1, lpwsz, 50);
    lpw += nchar;
    lpw = lpwAlign(lpw);    // Align creation data on DWORD boundary
    *lpw++ = 0;             // No creation data

    //-----------------------
    // Define a Help button.
    //-----------------------
    lpw = lpwAlign(lpw);    // Align DLGITEMTEMPLATE on DWORD boundary
    lpdit = (LPDLGITEMTEMPLATE)lpw;
    lpdit->x  = 55; lpdit->y  = 10;
    lpdit->cx = 40; lpdit->cy = 20;
    lpdit->id = ID_HELP;    // Help button identifier
    lpdit->style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;

    lpw = (LPWORD)(lpdit + 1);
    *lpw++ = 0xFFFF;
    *lpw++ = 0x0080;        // Button class atom

    lpwsz = (LPWSTR)lpw;
    nchar = 1 + MultiByteToWideChar(CP_ACP, 0, "Help", -1, lpwsz, 50);
    lpw += nchar;
    lpw = lpwAlign(lpw);    // Align creation data on DWORD boundary
    *lpw++ = 0;             // No creation data

    //-----------------------
    // Define a static text control.
    //-----------------------
    lpw = lpwAlign(lpw);    // Align DLGITEMTEMPLATE on DWORD boundary
    lpdit = (LPDLGITEMTEMPLATE)lpw;
    lpdit->x  = 10; lpdit->y  = 10;
    lpdit->cx = 40; lpdit->cy = 20;
    lpdit->id = ID_TEXT;    // Text identifier
    lpdit->style = WS_CHILD | WS_VISIBLE | SS_LEFT;

    lpw = (LPWORD)(lpdit + 1);
    *lpw++ = 0xFFFF;
    *lpw++ = 0x0082;        // Static class

    for (lpwsz = (LPWSTR)lpw; *lpwsz++ = (WCHAR)*lpszMessage++;);
    lpw = (LPWORD)lpwsz;
    lpw = lpwAlign(lpw);    // Align creation data on DWORD boundary
    *lpw++ = 0;             // No creation data

    GlobalUnlock(hgbl); 
    ret = DialogBoxIndirect(hinst, 
                           (LPDLGTEMPLATE)hgbl, 
                           hwndOwner, 
                           (DLGPROC)DialogProc); 
    GlobalFree(hgbl); 
    return ret; 
}
#endif

void AddControl(int x, int y, int w, int h, int type, int style, int n)
{
    int i;

    i = template.header.cdit++;
    template.control[i].item.style = style;
    template.control[i].item.dwExtendedStyle = 0;
    template.control[i].item.x = x;
    template.control[i].item.y = y;
    template.control[i].item.cx = w;
    template.control[i].item.cy = h;
    template.control[i].item.id = 2000 + n;
    template.control[i].code = 0xFFFF;
    template.control[i].controlType = type;
    template.control[i].d1 = ' ';
    template.control[i].data = 0;
    template.control[i].creationData = 0;
}

void AddOption(int x, int y, Control type, int i)
{

    switch(type) {
	case Spin:
	    AddControl(x, y+1, 95, 9, 0x0082, SS_ENDELLIPSIS | WS_VISIBLE | WS_CHILD, i);
	    AddControl(x+95, y, 50, 11, 0x0081, ES_AUTOHSCROLL | ES_NUMBER | WS_BORDER | WS_VISIBLE | WS_CHILD | WS_TABSTOP, i+1);
	    break;
//	case TextBox:
	    AddControl(x, y+1, 95, 9, 0x0082, SS_ENDELLIPSIS | WS_VISIBLE | WS_CHILD, i);
	    AddControl(x+95, y, 190, 11, 0x0081, ES_AUTOHSCROLL | WS_BORDER | WS_VISIBLE | WS_CHILD | WS_TABSTOP, i+1);
	    break;
	case TextBox:  // For now all text edits get a browse button, as long as -file and -path options are not yet implemented
	case FileName:
	case PathName:
	    AddControl(x, y+1, 95, 9, 0x0082, SS_ENDELLIPSIS | WS_VISIBLE | WS_CHILD, i);
	    AddControl(x+95, y, 180, 11, 0x0081, ES_AUTOHSCROLL | WS_BORDER | WS_VISIBLE | WS_CHILD | WS_TABSTOP, i+1);
	    AddControl(x+275, y, 20, 12, 0x0080, BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD | WS_TABSTOP, i-2);
	    layoutList[i/2-1] = -2;
	    break;
	case CheckBox:
	    AddControl(x, y, 145, 11, 0x0080, BS_AUTOCHECKBOX | WS_VISIBLE | WS_CHILD | WS_TABSTOP, i);
	    break;
	case ComboBox:
	    AddControl(x, y+1, 95, 9, 0x0082, SS_ENDELLIPSIS | WS_VISIBLE | WS_CHILD, i);
	    AddControl(x+95, y-1, 50, 500, 0x0085, CBS_AUTOHSCROLL | CBS_DROPDOWN | WS_VISIBLE | WS_CHILD | WS_TABSTOP, i+1);
	    break;
	case Button:
	    AddControl(x, y, 40, 15, 0x0080, BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD, i);
	    break;
    }
    
}

void
CreateDialogTemplate(int *layoutList, int nr, ChessProgramState *cps)
{
    int i, j, x=1, y=0, buttonRows, breakPoint = -1, k=0;

    template.header.cdit = 0;
    template.header.cx = 307;
    buttonRows = (buttons + 1 + 3)/4; // 4 per row, ronded up
    if(nr > 50) { 
	breakPoint = (nr+2*buttonRows+1)/2 & ~1;
	template.header.cx = 625;
    }

    for(i=0; i<nr; i++) {
	if(k < groups && i == boxList[k]) {
	    y += 10;
	    AddControl(x+2, y+13*(i>>1)-2, 301, 13*(boxList[k+1]-boxList[k]>>1)+8, 
						0x0082, WS_VISIBLE | WS_CHILD | SS_BLACKFRAME, 2400);
	    AddControl(x+60, y+13*(i>>1)-6, 10*groupNameList[k]/3, 10, 
						0x0082, SS_ENDELLIPSIS | WS_VISIBLE | WS_CHILD, 2*(i+MAX_OPTIONS));
	}
	j = layoutList[i];
	if(j >= 0)
	    AddOption(x+155-150*(i&1), y+13*(i>>1)+5, cps->option[j].type, 2*i);
	if(k < groups && i+1 == boxList[k+1]) {
	    k += 2; y += 4;
	}
	if(i+1 == breakPoint) { x += 318; y = -13*(breakPoint>>1); }
    }
    // add butons at the bottom of dialog window
    y += 13*(nr>>1)+5;

    AddControl(x+275, y+18*(buttonRows-1), 25, 15, 0x0080, BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD, IDOK-2000);
    AddControl(x+235, y+18*(buttonRows-1), 35, 15, 0x0080, BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD, IDCANCEL-2000);
    for(i=0; i<buttons; i++) {
	AddControl(x+70*(i%4)+5, y+18*(i/4), 65, 15, 0x0080, BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD, 2*(nr+i));
	layoutList[nr+i] = buttonList[i];
    }
    template.title[8] = cps == &first ? '1' :  '2';
    template.header.cy = y += 18*buttonRows+2;
    template.header.style &= ~WS_VSCROLL;
    if(y > 300, 0) {
	template.header.cx = 295;
	template.header.cy = 300;
	template.header.style |= WS_VSCROLL;
    }
}

void 
EngineOptionsPopup(HWND hwnd, ChessProgramState *cps)
{
    FARPROC lpProc = MakeProcInstance( (FARPROC) SettingsProc, hInst );

    activeCps = cps;
    DesignOptionDialog(cps);
    CreateDialogTemplate(layoutList, layout, cps);


    DialogBoxIndirect( hInst, &template.header, hwnd, (DLGPROC)lpProc );

    FreeProcInstance(lpProc);

    return;
}

