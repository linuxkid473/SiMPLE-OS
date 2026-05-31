/* kapp_constitution.c — United States Constitution Viewer */
#include "kapp.h"
#include "wm.h"
#include "string.h"

/* ================================================================
 * Colors: red, white, blue
 * ================================================================ */
#define CON_BG        0x04040EU   /* deep navy background          */
#define CON_TEXT      0xEEEEEEU   /* off-white body text           */
#define CON_DIM       0x7788AAU   /* dimmed / line-number text     */
#define CON_TITLE_FG  0xFFFFFFU   /* document title                */
#define CON_TITLE_BG  0x0A1A60U   /* title bar background          */
#define CON_SEC_FG    0xFFDD55U   /* section header foreground     */
#define CON_SEC_BG    0x0D0D2AU   /* section header background     */
#define CON_SUB_FG    0xAADDFFU   /* sub-section foreground        */
#define CON_AME_FG    0xFFAAAAU   /* amendment foreground          */
#define CON_RED       0xBB1111U   /* flag red                      */
#define CON_BLUE      0x1A2A88U   /* flag blue (canton)            */
#define CON_WHITE     0xF5F5F5U   /* flag white                    */
#define CON_GOLD      0xFFCC22U   /* gold accent                   */
#define CON_STATUS_BG 0x080814U   /* status bar background         */
#define CON_SEL_BG    0x1C2D70U   /* TOC selected item background  */
#define CON_SEL_FG    0xFFFFFFU   /* TOC selected item text        */
#define CON_FACT_BG   0x0A1A0AU   /* fact box background (dark green) */
#define CON_FACT_FG   0x88FFAAU   /* fact box text                 */

/* ================================================================
 * Document content
 * ================================================================ */
#define CON_NORMAL  0   /* plain body text                         */
#define CON_TITLE   1   /* large document title                    */
#define CON_SECTION 2   /* article / major section header          */
#define CON_AMEND   3   /* amendment header (red accent)           */
#define CON_SUB     4   /* sub-section / clause header             */
#define CON_BLANK   5   /* empty spacer line                       */
#define CON_FACT    6   /* highlighted fact / famous quote         */
#define CON_DIMTEXT 7   /* dimmed annotation line                  */

typedef struct { const char *text; uint8_t type; } con_line_t;

static const con_line_t con_doc[] = {
    /* -------- Title -------- */
    { "THE CONSTITUTION OF THE UNITED STATES", CON_TITLE  },
    { "",                                       CON_BLANK  },

    /* -------- Preamble -------- */
    { "PREAMBLE",                              CON_SECTION },
    { "We the People of the United States, in Order to form a", CON_NORMAL },
    { "more perfect Union, establish Justice, insure domestic",  CON_NORMAL },
    { "Tranquility, provide for the common defence, promote",    CON_NORMAL },
    { "the general Welfare, and secure the Blessings of",        CON_NORMAL },
    { "Liberty to ourselves and our Posterity, do ordain and",   CON_NORMAL },
    { "establish this Constitution for the United States.",       CON_NORMAL },
    { "",                                                         CON_BLANK  },

    /* -------- Article I -------- */
    { "ARTICLE I  —  THE LEGISLATIVE BRANCH",  CON_SECTION },
    { "Section 1. All legislative Powers herein granted shall", CON_NORMAL },
    { "be vested in a Congress of the United States, which",    CON_NORMAL },
    { "shall consist of a Senate and House of Representatives.", CON_NORMAL },
    { "",                                                        CON_BLANK  },
    { "Section 2. The House of Representatives shall be",       CON_NORMAL },
    { "composed of Members chosen every second Year by the",    CON_NORMAL },
    { "People of the several States. Representatives must be",  CON_NORMAL },
    { "at least 25 years old and a citizen for 7 years.",       CON_NORMAL },
    { "",                                                        CON_BLANK  },
    { "Section 3. The Senate shall be composed of two",         CON_NORMAL },
    { "Senators from each State, serving six-year terms.",      CON_NORMAL },
    { "Senators must be at least 30 years old and a citizen",   CON_NORMAL },
    { "for 9 years. The Vice President presides over the",      CON_NORMAL },
    { "Senate but may only vote to break a tie.",               CON_NORMAL },
    { "",                                                        CON_BLANK  },
    { "Section 7. All Bills for raising Revenue shall",         CON_NORMAL },
    { "originate in the House of Representatives. A Bill",      CON_NORMAL },
    { "passed by Congress becomes law when signed by the",      CON_NORMAL },
    { "President, or when two-thirds of both Houses override",  CON_NORMAL },
    { "a Presidential veto.",                                   CON_NORMAL },
    { "",                                                        CON_BLANK  },
    { "Section 8. Congress shall have Power to lay and",        CON_NORMAL },
    { "collect Taxes, borrow Money, regulate Commerce, coin",   CON_NORMAL },
    { "Money, establish Post Offices, declare War, raise",      CON_NORMAL },
    { "Armies, provide and maintain a Navy, and make all",      CON_NORMAL },
    { "Laws necessary and proper for carrying these Powers.",   CON_NORMAL },
    { "",                                                        CON_BLANK  },

    /* -------- Article II -------- */
    { "ARTICLE II  —  THE EXECUTIVE BRANCH",   CON_SECTION },
    { "Section 1. The executive Power shall be vested in a",   CON_NORMAL },
    { "President of the United States of America. The",        CON_NORMAL },
    { "President serves a four-year term and is elected via",  CON_NORMAL },
    { "the Electoral College. A President must be a natural-", CON_NORMAL },
    { "born citizen, at least 35 years old, and have been a",  CON_NORMAL },
    { "resident for 14 years.",                                 CON_NORMAL },
    { "",                                                        CON_BLANK  },
    { "Section 2. The President shall be Commander in Chief",  CON_NORMAL },
    { "of the Army and Navy. With Senate advice and consent,", CON_NORMAL },
    { "the President may make Treaties and appoint Judges of", CON_NORMAL },
    { "the Supreme Court and other Officers.",                  CON_NORMAL },
    { "",                                                        CON_BLANK  },
    { "Section 4. The President, Vice President, and all",     CON_NORMAL },
    { "civil Officers shall be removed from Office on",        CON_NORMAL },
    { "Impeachment for, and Conviction of, Treason, Bribery,", CON_NORMAL },
    { "or other high Crimes and Misdemeanors.",                 CON_NORMAL },
    { "",                                                        CON_BLANK  },

    /* -------- Article III -------- */
    { "ARTICLE III  —  THE JUDICIAL BRANCH",   CON_SECTION },
    { "Section 1. The judicial Power of the United States",    CON_NORMAL },
    { "shall be vested in one supreme Court, and in such",     CON_NORMAL },
    { "inferior Courts as the Congress may ordain.",           CON_NORMAL },
    { "Judges shall hold their Offices during good Behaviour.", CON_NORMAL },
    { "",                                                        CON_BLANK  },
    { "Section 2. The judicial Power shall extend to all",     CON_NORMAL },
    { "Cases arising under this Constitution, the Laws of",    CON_NORMAL },
    { "the United States, and Treaties. Trial of all Crimes",  CON_NORMAL },
    { "shall be by Jury.",                                      CON_NORMAL },
    { "",                                                        CON_BLANK  },
    { "Section 3. Treason against the United States shall",    CON_NORMAL },
    { "consist only in levying War against them, or in",       CON_NORMAL },
    { "adhering to their Enemies, giving them Aid and Comfort.", CON_NORMAL },
    { "",                                                        CON_BLANK  },

    /* -------- Article IV -------- */
    { "ARTICLE IV  —  THE STATES",              CON_SECTION },
    { "Section 1. Full Faith and Credit shall be given in",    CON_NORMAL },
    { "each State to the public Acts, Records, and judicial",   CON_NORMAL },
    { "Proceedings of every other State.",                      CON_NORMAL },
    { "",                                                        CON_BLANK  },
    { "Section 2. The Citizens of each State shall be",        CON_NORMAL },
    { "entitled to all Privileges and Immunities of Citizens", CON_NORMAL },
    { "in the several States.",                                 CON_NORMAL },
    { "",                                                        CON_BLANK  },
    { "Section 4. The United States shall guarantee to every", CON_NORMAL },
    { "State a Republican Form of Government, and shall",      CON_NORMAL },
    { "protect each of them against Invasion.",                 CON_NORMAL },
    { "",                                                        CON_BLANK  },

    /* -------- Article V -------- */
    { "ARTICLE V  —  THE AMENDMENT PROCESS",   CON_SECTION },
    { "Amendments may be proposed by two-thirds of both",      CON_NORMAL },
    { "Houses of Congress, or by a Constitutional Convention", CON_NORMAL },
    { "called by two-thirds of the State legislatures.",       CON_NORMAL },
    { "Ratification requires three-fourths of the States.",    CON_NORMAL },
    { "No State may be deprived of equal Senate suffrage",     CON_NORMAL },
    { "without its consent.",                                   CON_NORMAL },
    { "",                                                        CON_BLANK  },

    /* -------- Article VI -------- */
    { "ARTICLE VI  —  SUPREMACY CLAUSE",       CON_SECTION },
    { "This Constitution, and the Laws of the United States",  CON_NORMAL },
    { "which shall be made in Pursuance thereof, shall be",    CON_NORMAL },
    { "the supreme Law of the Land; the Judges in every",      CON_NORMAL },
    { "State shall be bound thereby.",                          CON_NORMAL },
    { "No religious Test shall ever be required as a",         CON_NORMAL },
    { "Qualification to any Office under the United States.",  CON_NORMAL },
    { "",                                                        CON_BLANK  },

    /* -------- Article VII -------- */
    { "ARTICLE VII  —  RATIFICATION",          CON_SECTION },
    { "The Ratification of the Conventions of nine States",    CON_NORMAL },
    { "shall be sufficient for the Establishment of this",     CON_NORMAL },
    { "Constitution. Done in Convention by Unanimous Consent", CON_NORMAL },
    { "of the States present, September 17, 1787.",             CON_NORMAL },
    { "",                                                        CON_BLANK  },

    /* -------- Bill of Rights header -------- */
    { "THE BILL OF RIGHTS  —  AMENDMENTS I-X", CON_SECTION },
    { "Ratified December 15, 1791",             CON_DIMTEXT },
    { "",                                        CON_BLANK  },
    { "\"A Bill of Rights is what the people are entitled to", CON_FACT   },
    { " against every government on earth.\"  — Thomas Jefferson", CON_FACT },
    { "",                                        CON_BLANK  },

    /* -------- Amendment I -------- */
    { "AMENDMENT I",                            CON_AMEND  },
    { "Congress shall make no law respecting an establishment", CON_NORMAL },
    { "of religion, or prohibiting the free exercise thereof;", CON_NORMAL },
    { "or abridging the freedom of speech, or of the press;",   CON_NORMAL },
    { "or the right of the people peaceably to assemble, and",  CON_NORMAL },
    { "to petition the Government for a redress of grievances.", CON_NORMAL },
    { "",                                                         CON_BLANK  },

    /* -------- Amendment II -------- */
    { "AMENDMENT II",                           CON_AMEND  },
    { "A well regulated Militia, being necessary to the",       CON_NORMAL },
    { "security of a free State, the right of the people to",   CON_NORMAL },
    { "keep and bear Arms, shall not be infringed.",             CON_NORMAL },
    { "",                                                         CON_BLANK  },

    /* -------- Amendment III -------- */
    { "AMENDMENT III",                          CON_AMEND  },
    { "No Soldier shall, in time of peace be quartered in",     CON_NORMAL },
    { "any house, without the consent of the Owner, nor in",    CON_NORMAL },
    { "time of war, but in a manner prescribed by law.",        CON_NORMAL },
    { "",                                                         CON_BLANK  },

    /* -------- Amendment IV -------- */
    { "AMENDMENT IV",                           CON_AMEND  },
    { "The right of the people to be secure in their persons,", CON_NORMAL },
    { "houses, papers, and effects, against unreasonable",      CON_NORMAL },
    { "searches and seizures, shall not be violated, and no",   CON_NORMAL },
    { "Warrants shall issue, but upon probable cause.",          CON_NORMAL },
    { "",                                                         CON_BLANK  },

    /* -------- Amendment V -------- */
    { "AMENDMENT V",                            CON_AMEND  },
    { "No person shall be held to answer for a capital crime", CON_NORMAL },
    { "without grand jury indictment, nor be twice put in",    CON_NORMAL },
    { "jeopardy, nor be compelled to be a witness against",    CON_NORMAL },
    { "himself, nor deprived of life, liberty, or property",   CON_NORMAL },
    { "without due process of law.",                            CON_NORMAL },
    { "",                                                        CON_BLANK  },

    /* -------- Amendment VI -------- */
    { "AMENDMENT VI",                           CON_AMEND  },
    { "In all criminal prosecutions, the accused shall enjoy", CON_NORMAL },
    { "the right to a speedy and public trial, by an",         CON_NORMAL },
    { "impartial jury, to be informed of the accusation,",     CON_NORMAL },
    { "to confront witnesses, and to have counsel.",            CON_NORMAL },
    { "",                                                        CON_BLANK  },

    /* -------- Amendment VII -------- */
    { "AMENDMENT VII",                          CON_AMEND  },
    { "In suits at common law where the value in controversy", CON_NORMAL },
    { "exceeds twenty dollars, the right of trial by jury",    CON_NORMAL },
    { "shall be preserved.",                                    CON_NORMAL },
    { "",                                                        CON_BLANK  },

    /* -------- Amendment VIII -------- */
    { "AMENDMENT VIII",                         CON_AMEND  },
    { "Excessive bail shall not be required, nor excessive",   CON_NORMAL },
    { "fines imposed, nor cruel and unusual punishments",      CON_NORMAL },
    { "inflicted.",                                             CON_NORMAL },
    { "",                                                        CON_BLANK  },

    /* -------- Amendment IX -------- */
    { "AMENDMENT IX",                           CON_AMEND  },
    { "The enumeration in the Constitution, of certain",       CON_NORMAL },
    { "rights, shall not be construed to deny or disparage",   CON_NORMAL },
    { "others retained by the people.",                        CON_NORMAL },
    { "",                                                        CON_BLANK  },

    /* -------- Amendment X -------- */
    { "AMENDMENT X",                            CON_AMEND  },
    { "The powers not delegated to the United States by the", CON_NORMAL },
    { "Constitution, nor prohibited by it to the States,",    CON_NORMAL },
    { "are reserved to the States respectively, or to the",   CON_NORMAL },
    { "people.",                                               CON_NORMAL },
    { "",                                                       CON_BLANK  },

    /* -------- Later Amendments -------- */
    { "SUBSEQUENT AMENDMENTS  —  XI THROUGH XXVII", CON_SECTION },
    { "",                                             CON_BLANK  },

    { "AMENDMENT XI  (1795)",                   CON_AMEND  },
    { "Limits federal judicial power to suits by citizens",    CON_NORMAL },
    { "of other states against a state government.",           CON_NORMAL },
    { "",                                                        CON_BLANK  },

    { "AMENDMENT XII  (1804)",                  CON_AMEND  },
    { "Requires separate Electoral College ballots for",       CON_NORMAL },
    { "President and Vice President.",                         CON_NORMAL },
    { "",                                                        CON_BLANK  },

    { "AMENDMENT XIII  (1865)",                 CON_AMEND  },
    { "Neither slavery nor involuntary servitude, except as", CON_NORMAL },
    { "punishment for crime, shall exist in the United States.", CON_NORMAL },
    { "",                                                         CON_BLANK  },

    { "AMENDMENT XIV  (1868)",                  CON_AMEND  },
    { "All persons born or naturalized in the United States", CON_NORMAL },
    { "are citizens. No State shall deprive any person of",   CON_NORMAL },
    { "life, liberty, or property without due process, nor",  CON_NORMAL },
    { "deny to any person equal protection of the laws.",     CON_NORMAL },
    { "",                                                       CON_BLANK  },

    { "AMENDMENT XV  (1870)",                   CON_AMEND  },
    { "The right to vote shall not be denied on account of",  CON_NORMAL },
    { "race, color, or previous condition of servitude.",     CON_NORMAL },
    { "",                                                       CON_BLANK  },

    { "AMENDMENT XVI  (1913)",                  CON_AMEND  },
    { "Congress shall have power to lay and collect taxes",   CON_NORMAL },
    { "on incomes, from whatever source derived.",            CON_NORMAL },
    { "",                                                       CON_BLANK  },

    { "AMENDMENT XVII  (1913)",                 CON_AMEND  },
    { "The Senate shall be composed of two Senators from",    CON_NORMAL },
    { "each State, elected by the people thereof.",           CON_NORMAL },
    { "",                                                       CON_BLANK  },

    { "AMENDMENT XVIII  (1919)",                CON_AMEND  },
    { "Prohibited the manufacture, sale, and transportation", CON_NORMAL },
    { "of intoxicating liquors (Prohibition).",               CON_NORMAL },
    { "",                                                       CON_BLANK  },

    { "AMENDMENT XIX  (1920)",                  CON_AMEND  },
    { "The right to vote shall not be denied on account of",  CON_NORMAL },
    { "sex. (Women's Suffrage)",                              CON_NORMAL },
    { "",                                                       CON_BLANK  },

    { "AMENDMENT XX  (1933)",                   CON_AMEND  },
    { "Sets the start of Presidential and Congressional",     CON_NORMAL },
    { "terms (January 20 and January 3, respectively).",      CON_NORMAL },
    { "",                                                       CON_BLANK  },

    { "AMENDMENT XXI  (1933)",                  CON_AMEND  },
    { "Repeals Prohibition (Amendment XVIII).",               CON_NORMAL },
    { "",                                                       CON_BLANK  },

    { "AMENDMENT XXII  (1951)",                 CON_AMEND  },
    { "No person shall be elected President more than twice.", CON_NORMAL },
    { "",                                                        CON_BLANK  },

    { "AMENDMENT XXIII  (1961)",                CON_AMEND  },
    { "Grants the District of Columbia electors in the",      CON_NORMAL },
    { "Electoral College.",                                    CON_NORMAL },
    { "",                                                       CON_BLANK  },

    { "AMENDMENT XXIV  (1964)",                 CON_AMEND  },
    { "The right to vote shall not be denied for failure",    CON_NORMAL },
    { "to pay any poll tax or other tax.",                    CON_NORMAL },
    { "",                                                       CON_BLANK  },

    { "AMENDMENT XXV  (1967)",                  CON_AMEND  },
    { "Establishes Presidential succession and provides for", CON_NORMAL },
    { "filling a vacancy in the Vice Presidency.",            CON_NORMAL },
    { "",                                                       CON_BLANK  },

    { "AMENDMENT XXVI  (1971)",                 CON_AMEND  },
    { "The right to vote shall not be denied to citizens",   CON_NORMAL },
    { "eighteen years of age or older.",                      CON_NORMAL },
    { "",                                                       CON_BLANK  },

    { "AMENDMENT XXVII  (1992)",                CON_AMEND  },
    { "No law varying the compensation of Senators and",     CON_NORMAL },
    { "Representatives shall take effect until an election", CON_NORMAL },
    { "of Representatives shall have intervened.",           CON_NORMAL },
    { "",                                                      CON_BLANK  },

    { "--- END OF DOCUMENT ---",               CON_DIMTEXT },
};

#define CON_NLINES ((int)(sizeof(con_doc) / sizeof(con_doc[0])))

/* ================================================================
 * Table of Contents
 * ================================================================ */
#define CON_TOC_MAX 40

static struct { const char *name; int line; } con_toc[CON_TOC_MAX];
static int con_ntoc;

/* ================================================================
 * App state
 * ================================================================ */
static int con_scroll;      /* top visible line in doc view     */
static int con_toc_mode;    /* 1 = TOC, 0 = document            */
static int con_toc_sel;     /* selected TOC item                 */
static int con_vis_lines;   /* visible doc lines (computed each render) */

/* ================================================================
 * Lifecycle
 * ================================================================ */
void constitution_create(int wi) {
    (void)wi;
    con_scroll   = 0;
    con_toc_mode = 1;
    con_toc_sel  = 0;
    con_vis_lines = 20;
    con_ntoc = 0;

    for (int i = 0; i < CON_NLINES && con_ntoc < CON_TOC_MAX; i++) {
        uint8_t t = con_doc[i].type;
        if (t == CON_TITLE || t == CON_SECTION || t == CON_AMEND) {
            con_toc[con_ntoc].name = con_doc[i].text;
            con_toc[con_ntoc].line = i;
            con_ntoc++;
        }
    }
}

void constitution_destroy(int wi) { (void)wi; }
void constitution_tick(int wi)    { (void)wi; }
void constitution_click(int wi, int x, int y) {
    (void)wi; (void)x; (void)y;
}
void constitution_mouse(int wi, int x, int y, int btn) {
    (void)wi; (void)x; (void)y; (void)btn;
}

/* ================================================================
 * Key handling
 * ================================================================ */
void constitution_key(int wi, int kt, char ch) {
    if (con_toc_mode) {
        /* TOC navigation */
        if (kt == KEY_EVENT_UP) {
            if (con_toc_sel > 0) con_toc_sel--;
        } else if (kt == KEY_EVENT_DOWN) {
            if (con_toc_sel < con_ntoc - 1) con_toc_sel++;
        } else if (kt == KEY_EVENT_ENTER) {
            /* Jump to selected TOC entry */
            con_scroll   = con_toc[con_toc_sel].line;
            con_toc_mode = 0;
        } else if (ch == 27) {   /* ESC closes */
            wm_close_kapp(wi);
        } else if (ch == ' ') {  /* Space also jumps to selection */
            con_scroll   = con_toc[con_toc_sel].line;
            con_toc_mode = 0;
        }
    } else {
        /* Document navigation */
        if (kt == KEY_EVENT_UP) {
            if (con_scroll > 0) con_scroll--;
        } else if (kt == KEY_EVENT_DOWN) {
            if (con_scroll < CON_NLINES - 1) con_scroll++;
        } else if (ch == ' ') {
            con_scroll += con_vis_lines;
            if (con_scroll >= CON_NLINES) con_scroll = CON_NLINES - 1;
        } else if (ch == 'b' || ch == 'B') {
            con_scroll -= con_vis_lines;
            if (con_scroll < 0) con_scroll = 0;
        } else if (ch == 't' || ch == 'T' || ch == 27) {
            /* T or ESC returns to TOC */
            con_toc_mode = 1;
        } else if (ch == 'h' || ch == 'H') {
            con_scroll = 0;
        }
    }
}

/* ================================================================
 * Drawing helpers
 * ================================================================ */

/* Procedurally draw the US flag in the given rectangle */
static void draw_flag(int x, int y, int w, int h) {
    int stripe_h = h / 13;
    if (stripe_h < 1) stripe_h = 1;

    /* 13 alternating red/white stripes */
    for (int i = 0; i < 13; i++) {
        uint32_t col = (i % 2 == 0) ? CON_RED : CON_WHITE;
        int sy = y + i * stripe_h;
        int sh = (i == 12) ? (y + h - sy) : stripe_h;
        kd_fill(x, sy, w, sh, col);
    }

    /* Blue canton: covers first 7 stripes on the left */
    int canton_w = w * 2 / 5;
    int canton_h = stripe_h * 7;
    if (canton_w < 1) canton_w = 1;
    kd_fill(x, y, canton_w, canton_h, CON_BLUE);

    /* Stars: 5×6 grid of white dots in the canton */
    if (canton_w >= 20 && canton_h >= 14) {
        int cols = 6, rows = 5;
        int sx0 = x + canton_w / (cols * 2);
        int sy0 = y + canton_h / (rows * 2);
        int dx  = canton_w / cols;
        int dy  = canton_h / rows;
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                int star_x = sx0 + c * dx;
                int star_y = sy0 + r * dy;
                kd_fill(star_x,     star_y - 1, 2, 1, CON_WHITE);
                kd_fill(star_x - 1, star_y,     4, 1, CON_WHITE);
                kd_fill(star_x,     star_y + 1, 2, 1, CON_WHITE);
            }
        }
    }

    /* Thin gold border around flag */
    kd_rect(x, y, w, h, CON_GOLD);
}

/* Draw a single document line at pixel y */
static void draw_doc_line(int x, int y, int w, int li) {
    if (li < 0 || li >= CON_NLINES) return;
    const con_line_t *l = &con_doc[li];
    switch (l->type) {
    case CON_TITLE:
        kd_fill(x, y, w, 10, CON_TITLE_BG);
        kd_str(x + 4, y + 1, l->text, CON_TITLE_FG, CON_TITLE_BG);
        break;
    case CON_SECTION:
        kd_fill(x, y, w, 10, CON_SEC_BG);
        kd_hline(x, y, w, 0x223088U);
        kd_str(x + 4, y + 1, l->text, CON_SEC_FG, CON_SEC_BG);
        break;
    case CON_AMEND:
        kd_fill(x, y, w, 10, CON_BG);
        kd_str(x + 4, y + 1, l->text, CON_AME_FG, CON_BG);
        kd_hline(x + 4, y + 9, (int)strlen(l->text) * 8, 0x883333U);
        break;
    case CON_SUB:
        kd_str(x + 4, y + 1, l->text, CON_SUB_FG, CON_BG);
        break;
    case CON_FACT:
        kd_fill(x, y, w, 10, CON_FACT_BG);
        kd_str(x + 4, y + 1, l->text, CON_FACT_FG, CON_FACT_BG);
        break;
    case CON_BLANK:
        break;
    case CON_DIMTEXT:
        kd_str(x + 4, y + 1, l->text, CON_DIM, CON_BG);
        break;
    default:
        kd_str(x + 4, y + 1, l->text, CON_TEXT, CON_BG);
        break;
    }
}

/* ================================================================
 * Render
 * ================================================================ */
void constitution_render(int wi, int cx, int cy, int cw, int ch) {
    (void)wi;

    /* Clear background */
    kd_fill(cx, cy, cw, ch, CON_BG);

    /* Flag banner at top */
    int flag_h = 52;
    int flag_w = cw - 2;
    draw_flag(cx + 1, cy + 1, flag_w, flag_h);

    /* Title text over flag */
    kd_str(cx + flag_w / 2 - 136, cy + flag_h / 2 - 8,
           "The Constitution of the United States",
           CON_TITLE_FG, 0);

    int content_y  = cy + flag_h + 2;
    int content_h  = ch - flag_h - 2 - 14; /* leave room for status bar */
    int line_h     = 10;

    con_vis_lines = content_h / line_h;
    if (con_vis_lines < 1) con_vis_lines = 1;

    if (con_toc_mode) {
        /* ---- Table of Contents ---- */
        kd_fill(cx, content_y, cw, 14, CON_SEC_BG);
        kd_hline(cx, content_y + 13, cw, CON_GOLD);
        kd_str(cx + 4, content_y + 3, "TABLE OF CONTENTS  —  Up/Dn:navigate  Enter/Spc:jump  ESC:close",
               CON_GOLD, CON_SEC_BG);
        content_y += 15;

        int toc_vis = (content_h - 15) / line_h;
        if (toc_vis < 1) toc_vis = 1;

        /* Scroll TOC so selected item is always visible */
        int toc_scroll = con_toc_sel - toc_vis / 2;
        if (toc_scroll < 0) toc_scroll = 0;
        if (toc_scroll + toc_vis > con_ntoc) toc_scroll = con_ntoc - toc_vis;
        if (toc_scroll < 0) toc_scroll = 0;

        for (int i = 0; i < toc_vis; i++) {
            int ti = toc_scroll + i;
            if (ti >= con_ntoc) break;
            int iy = content_y + i * line_h;
            int sel = (ti == con_toc_sel);

            uint32_t bg = sel ? CON_SEL_BG : CON_BG;
            kd_fill(cx, iy, cw, line_h, bg);

            /* Number */
            char num[6];
            kd_itoa(ti + 1, num, sizeof(num));
            kd_str(cx + 4, iy + 1, num, sel ? CON_GOLD : CON_DIM, bg);

            /* Section name — color by type */
            uint8_t dtype = con_doc[con_toc[ti].line].type;
            uint32_t fg;
            if (sel)                  fg = CON_SEL_FG;
            else if (dtype == CON_TITLE)   fg = CON_TITLE_FG;
            else if (dtype == CON_SECTION) fg = CON_SEC_FG;
            else                           fg = CON_AME_FG;

            kd_str(cx + 28, iy + 1, con_toc[ti].name, fg, bg);

            /* Arrow indicator for selected */
            if (sel) kd_str(cx + cw - 16, iy + 1, ">", CON_GOLD, bg);
        }

        /* Gold bottom separator */
        kd_hline(cx, content_y + toc_vis * line_h, cw, CON_GOLD);

    } else {
        /* ---- Document view ---- */

        /* Clamp scroll */
        if (con_scroll < 0) con_scroll = 0;
        if (con_scroll >= CON_NLINES) con_scroll = CON_NLINES - 1;

        for (int i = 0; i < con_vis_lines; i++) {
            int li = con_scroll + i;
            if (li >= CON_NLINES) break;
            int iy = content_y + i * line_h;
            kd_fill(cx, iy, cw, line_h, CON_BG);
            draw_doc_line(cx, iy, cw, li);
        }

        /* Scrollbar — right edge */
        {
            int sb_x = cx + cw - 5;
            int sb_h = con_vis_lines * line_h;
            kd_fill(sb_x, content_y, 4, sb_h, 0x0C0C22U);
            if (CON_NLINES > con_vis_lines) {
                int thumb_h = sb_h * con_vis_lines / CON_NLINES;
                if (thumb_h < 4) thumb_h = 4;
                int thumb_y = content_y + (sb_h - thumb_h) * con_scroll / (CON_NLINES - con_vis_lines);
                kd_fill(sb_x + 1, thumb_y, 2, thumb_h, CON_BLUE);
            }
        }
    }

    /* Status bar */
    int sb_y = cy + ch - 14;
    kd_fill(cx, sb_y, cw, 14, CON_STATUS_BG);
    kd_hline(cx, sb_y, cw, CON_GOLD);
    if (con_toc_mode) {
        kd_str(cx + 4, sb_y + 3,
               "Up/Dn:select  Enter:jump to section  ESC:close",
               CON_DIM, CON_STATUS_BG);
    } else {
        char pos[20];
        pos[0] = 'L'; pos[1] = 'n'; pos[2] = ' ';
        kd_itoa(con_scroll + 1, pos + 3, 12);
        int pi = 3;
        while (pos[pi]) pi++;
        pos[pi++] = '/';
        kd_itoa(CON_NLINES, pos + pi, 10);
        kd_str(cx + 4, sb_y + 3, pos, CON_DIM, CON_STATUS_BG);
        kd_str(cx + 80, sb_y + 3,
               "Up/Dn:scroll  Spc/B:page  H:top  T/ESC:contents",
               CON_DIM, CON_STATUS_BG);
    }
}
