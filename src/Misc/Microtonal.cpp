/*
    Microtonal.cpp - Tuning settings and microtonal capabilities

    Original ZynAddSubFX author Nasca Octavian Paul
    Copyright (C) 2002-2005 Nasca Octavian Paul
    Copyright 2009-2011, Alan Calvert
    Copyright 2017-2023, Will Godfrey

    This file is part of yoshimi, which is free software: you can redistribute
    it and/or modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either version 2 of
    the License, or (at your option) any later version.

    yoshimi is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.   See the GNU General Public License (version 2 or
    later) for more details.

    You should have received a copy of the GNU General Public License along with
    yoshimi; if not, write to the Free Software Foundation, Inc., 51 Franklin
    Street, Fifth Floor, Boston, MA  02110-1301, USA.

    This file is derivative of original ZynAddSubFX code.
*/

#include <cmath>
#include <iostream>
#include <algorithm>
#include <limits.h>

#include "Misc/Config.h"
#include "Misc/XMLwrapper.h"
#include "Misc/Microtonal.h"
#include "Misc/SynthEngine.h"
#include "Misc/NumericFuncs.h"
#include "Misc/FormatFuncs.h"
#include "Misc/FileMgrFuncs.h"

using func::power;
using file::loadText;
using file::findLeafName;
using std::cout;
using std::endl;
using std::string;
using std::to_string;
using std::to_string;


namespace { // local implementation details

    const size_t BUFFSIZ = 1024;      // for loading KBM or SCL settings from a file
    const size_t MAX_LINE_SIZE = 256; // for converting text to mappings or tunings
    // sizes increased to allow for more comments


    inline void prepareText(string &text)
    {
        size_t pos = 0;
        while (text[pos] == ' ') // skip leading spaces
            pos += 1;

        while (text[pos] == '0' && isdigit(text[pos + 1])) // skip leading zeros
            pos += 1;
        text = text.substr(pos, text.length());

    }


    inline string lineInText(string text, size_t &point)
    {
        size_t len = text.length();
        if (point >= len - 1)
            return "";
        size_t it = 0;
        while (it < len - point && text.at(point + it) >= ' ')
            ++it;
        string line = text.substr(point, it);
        point += (it + 1);
        return line;
    }


    inline bool fetchLineFromText(string text, size_t &point, char *line, size_t length)
    {
        bool ok = true;
        string found = lineInText(text, point);
        prepareText(found);
        if (found == "")
            line[0] = 0;
        else if (found.length() < (length - 1))
        {
            strcpy(line, found.c_str());
            line[length] = 0;
            // cout << "fetch " << line << endl;
        }
        else
        {
            ok = false;
            line[0] = 0;
        }
        return ok;
    }

}//(End)implementation details


void Microtonal::defaults(int type)
{
    if (type != 2) // not map
    {
        Pinvertupdown = 0;
        Pinvertupdowncenter = 60;
        octavesize = 12;
        Penabled = 0;
        PrefNote = 69;
        PrefFreq = 440.0f;
        Pscaleshift = 64;
        octave[11].type = 2;
        octave[11].x1 = 2;
        octave[11].x2 = 1;
        Pname = string("12tET");
        Pcomment = string("Default Tuning");

    }
    if (type != 1) // not tuning
    {
        Pfirstkey = 0;
        Plastkey = 127;
        Pmiddlenote = 60;
        Pmapsize = 12;
        Pmappingenabled = 0;

        for (int i = 0; i < 128; ++i)
        {
            Pmapping[i] = i;
            PmapComment[i] = "";
        }
        Pcomment = string("Default Map");
    }

    for (size_t i = 0; i < MAX_OCTAVE_SIZE; ++i)
    {
        octave[i].text = reformatline(to_string((i % octavesize + 1) * 100)+ ".0");
        octave[i].tuning = pow(2.0, (i % octavesize + 1) / 12.0);
        octave[i].type = 1;
        octave[i].x1 = (i % octavesize + 1) * 100;
        octave[i].x2 = 0;
        octave[i].comment = "";
    }
    if (type == 0)
    {
        octave[11].type = 2;
        octave[11].x1 = 2;
        octave[11].x2 = 1;
        Pname = string("12tET");
        Pcomment = string("Equal Temperament 12 notes per octave");
    }
    Pglobalfinedetune = 64.0; // always set this
}


// Get the frequency according to the note number
float Microtonal::getNoteFreq(int note, int keyshift)
{
    // in this function will appears many times things like this:
    // var=(a+b*100)%b
    // I had written this way because if I use var=a%b gives unwanted results when a<0
    // This is the same with divisions.

    if ((Pinvertupdown != 0) && ((Pmappingenabled == 0) || (Penabled == 0)))
        note = Pinvertupdowncenter * 2 - note;

    // compute global fine detune, -64.0 .. 63.0 cents
    float globalfinedetunerap =
        (Pglobalfinedetune > 64.0f || Pglobalfinedetune < 64.0f)
            ? power<2>((Pglobalfinedetune - 64.0f) / 1200.0f)
            : 1.0f;
    // was float globalfinedetunerap = powf(2.0f, (Pglobalfinedetune - 64.0f) / 1200.0f);

    if (!Penabled)
        return getFixedNoteFreq(note + keyshift) * globalfinedetunerap;


    int scaleshift = (Pscaleshift - 64 + octavesize * 100) % octavesize;

    // compute the keyshift
    float rap_keyshift = 1.0f;
    if (keyshift)
    {
        int kskey = (keyshift + octavesize * 100) % octavesize;
        int ksoct = (keyshift + octavesize * 100) / octavesize - 100;
        rap_keyshift  = (!kskey) ? 1.0f : (octave[kskey - 1].tuning);
        rap_keyshift *= powf(octave[octavesize - 1].tuning, ksoct);
    }

    float freq;
    // if the mapping is enabled
    if (Pmappingenabled)
    {
        if ((note < Pfirstkey) || (note > Plastkey))
            return -1.0f;
        // Compute how many mapped keys are from middle note to reference note
        // and find out the proportion between the freq. of middle note and "A" note
        int tmp = PrefNote - Pmiddlenote;
        int minus = 0;
        if (tmp < 0)
        {
            tmp   = -tmp;
            minus = 1;
        }
        int deltanote = 0;
        for (int i = 0; i < tmp; ++i)
            if (Pmapping[i % Pmapsize] >= 0)
                deltanote++;
        float rap_anote_middlenote =
            (deltanote == 0) ? (1.0f) : (octave[(deltanote - 1) % octavesize].tuning);
        if (deltanote != 0)
            rap_anote_middlenote *= powf(octave[octavesize - 1].tuning,
                                         (deltanote - 1) / octavesize);
        if (minus)
            rap_anote_middlenote = 1.0f / rap_anote_middlenote;

        // Convert from note (midi) to degree (note from the tuning)
        int degoct = (note - Pmiddlenote + Pmapsize * 200)
                      / Pmapsize - 200;
        int degkey = (note - Pmiddlenote + Pmapsize * 100) % Pmapsize;
        degkey = Pmapping[degkey];
        if (degkey < 0) // this key is not mapped
            return -1.0f;

        // invert the keyboard upside-down if it is asked for
        // TODO: do the right way by using Pinvertupdowncenter
        if (Pinvertupdown)
        {
            degkey = octavesize - degkey - 1;
            degoct = -degoct;
        }
        // compute the frequency of the note
        degkey  = degkey + scaleshift;
        degoct += degkey / octavesize;
        degkey %= octavesize;

        freq = (degkey == 0) ? (1.0f) : octave[degkey - 1].tuning;
        freq *= powf(octave[octavesize - 1].tuning, degoct);
        freq *= PrefFreq / rap_anote_middlenote;
    }
    else // if the mapping is disabled
    {
        int nt = note - PrefNote + scaleshift;
        int ntkey = (nt + octavesize * 100) % octavesize;
        // cast octavesize to a signed type so the expression stays signed
        int ntoct = (nt - ntkey) / int(octavesize);

        float oct  = octave[octavesize - 1].tuning;
        freq = octave[(ntkey + octavesize - 1) % octavesize].tuning
               * powf(oct, ntoct) * PrefFreq;
        if (ntkey == 0)
            freq /= oct;
    }
    if (scaleshift != 0)
        freq /= octave[scaleshift - 1].tuning;
    freq *= globalfinedetunerap;
    return freq * rap_keyshift;
}


string Microtonal::reformatline(string text)
{
    //text.erase(remove_if (text.begin(), text.end(), [](char c){ return (c =='\r' || c =='\t' || c == ' ' || c == '\n');}), text.end() );

    // above replaced as it screwed up comments in lines

    string formattext = "";
    char Chr;
    for (size_t i = 0; i < text.length(); ++i)
    {
        Chr = text[i];
        if (Chr == '.' || Chr == '/' || (Chr >= '0' && Chr <= '9'))
            formattext += Chr;
    }
    //cout << "Formatted >" << formattext << endl;
    size_t found;
    found = formattext.find('.');
    if (found < 4)
    {
        string tmp (4 - found, '0'); // leading zeros
        formattext = tmp + formattext;
    }
    found = formattext.size();
    if (found < 11)
    {
        string tmp  (11 - found, '0'); // trailing zeros
        formattext += tmp;
    }
    return formattext;
}


bool Microtonal::validline(const char *line)
{
    int idx = 0;
    bool ok = true;
    while (ok && line[idx] > 31)
    {
        char chr = line[idx];
        if (chr != ' ' && chr != '.' && chr != '/' && (chr < '0' || chr > '9'))
            ok = false;
        ++ idx;
    }
    return ok;
}


// Convert a line to tunings; returns 0 if ok
int Microtonal::linetotunings(unsigned int nline, const char *line)
{
    string last = string(line);
    char local [MAX_LINE_SIZE];
    strcpy(local, line);
    size_t pos = last.find("!");
    if (pos != string::npos)
    {
        last = last.substr(pos + 1, last.length());
        octave[nline].comment = func::trimEnds(last);
    }
    else
        octave[nline].comment = "";
    local[pos] = 0;

    if (!validline(local))
        return SCALES::errors::badNumbers;

    int x1 = -1, x2 = -1, type = -1;
    double x = -1.0;
    double tuning = 1.0;
    if (strstr(local, "."))
    {
            x = stod(string(local));
            if (x < 0.000001)
                return SCALES::errors::valueTooSmall;
            type = 1; // double type(cents)
            x1 = int(floor(x));
            double tmp = fmod(x, 1.0);
            x2 = int(floor(tmp * 1e6));
            tuning = pow(2.0, x / 1200.0);
            octave[nline].text = reformatline(local);
    }
    else
    {
        if (strstr(local, "/"))
        {
            sscanf(local, "%d/%d", &x1, &x2);
            if (x1 < 0 || x2 < 0)
                return SCALES::errors::badNumbers;
            if (!x2)
                x2 = 1;
            type = 2; // division
        }
        else
        {
            sscanf(local, "%d", &x1);
            x2 = 1;
            type = 2; // division by 1
        }

        if (x1 <= 0)
        x1 = 1; // not allow zero frequency sounds (consider 0 as 1)
        tuning = double(x1) / x2;
    }

    octave[nline].tuning = tuning;
    octave[nline].type = type;
    octave[nline].x1 = x1;
    octave[nline].x2 = x2;
    return 0; // ok
}


// Convert the text to tunings
int Microtonal::texttotunings(const char *text)
{
    size_t i;
    unsigned int k = 0, nl = 0;
    char *lin;

    lin = new char[MAX_LINE_SIZE + 1];
    while (k < strlen(text))
    {
        for (i = 0; i < MAX_LINE_SIZE; ++i)
        {
            lin[i] = text[k++];
            if (lin[i] < 0x20)
                break;
        }
        lin[i] = '\0';
        if (!strlen(lin))
            continue;
        int err = linetotunings(nl, lin);
        if (err != 0)
        {
            delete [] lin;
            return err; // Parse error
        }
        nl++;
    }
    delete [] lin;
    if (nl > MAX_OCTAVE_SIZE)
        nl = MAX_OCTAVE_SIZE;
    if (!nl)
        return 0; // the input is empty
    octavesize = nl;
    return octavesize; // ok
}


// Convert the text to mapping
int Microtonal::texttomapping(const char *text)
{
    unsigned int i, k = 0;
    char *lin;
    lin = new char[MAX_LINE_SIZE + 1];
    memset(lin, 0xff, MAX_LINE_SIZE);
    int tx = 0;
    while (k < strlen(text))
    {
        for (i = 0; i < MAX_LINE_SIZE; ++i)
        {
            lin[i] = text[k++];
            if (lin[i] < 0x20)
            {
                break;
            }
        }
        lin[i] = 0;

        string alt = func::trimEnds(lin);

        size_t pos = alt.find('!');
        if (pos != string::npos)
        {
            PmapComment[tx] = func::trimEnds(alt.substr(pos + 1, alt.length()));
        }
        else
            PmapComment[tx] = "";

        if (alt.empty() || alt[0] < '0' || alt[0] > '9')
        {
            alt = 'x';
            Pmapping[tx] = -1;
        }
        else
            Pmapping[tx] = stoi(alt);
        tx++;
    }
    if (tx)
    {
        Pmapsize = tx;
    }
    else
        return SCALES::errors::badMapSize;
    return tx;
}


string Microtonal::keymaptotext(void)
{
    string text;
    for (int i = 0; i < Pmapsize; ++i)
    {
        if (i > 0)
            text += "\n";
        if (Pmapping[i] == -1)
            text += "x";
        else
            text += to_string(Pmapping[i]);
        if (!PmapComment[i].empty())
        {
            text += " ! ";
            text += PmapComment[i];
        }
    }
    return text;
}

// Convert tuning to text line
void Microtonal::tuningtoline(unsigned int n, char *line, int maxn)
{
    if (n > octavesize || n > MAX_OCTAVE_SIZE)
    {
        line[0] = '\0';
        return;
    }
    string text = octave[n].text;
    if (octave[n].type == 1)
    {
        if (text > " ")
            snprintf(line, maxn, "%s", text.c_str());
        else
            snprintf(line, maxn, "%04d.%06d", octave[n].x1,octave[n].x2);
    }
    else if (octave[n].type == 2)
        snprintf(line, maxn, "%d/%d", octave[n].x1, octave[n].x2);
}


string Microtonal::tuningtotext()
{
    string text;
    char *buff = new char[BUFFSIZ];
    for (size_t i = 0; i < octavesize; ++i)
    {
        if (i > 0)
            text += "\n";
        tuningtoline(i, buff, MAX_LINE_SIZE);
        text += string(buff);
        if (!octave[i].comment.empty())
        {
            text += " ! ";
            text += octave[i].comment;
        }
    }
    delete [] buff;
    return text;
}


int Microtonal::loadLine(const string& text, size_t &point, char *line, size_t maxlen)
{
    do {
        line[0] = 0;
        fetchLineFromText(text, point, line, maxlen);
    } while (line[0] == '!'); // skip over these
    if (line[0] < ' ')
        return SCALES::errors::missingEntry;
    return 0;
}


// Loads the tunings from a scl file
int Microtonal::loadscl(const string& filename)
{
    string text = loadText(filename);
    if (text == "")
        return SCALES::errors::noFile;
    char tmp[BUFFSIZ];
    size_t point = 0;
    int err = 0;
    int nnotes;

    // loads the short description
    if (loadLine(text, point, tmp, BUFFSIZ))
        err = SCALES::errors::emptyFile;
    if (err == 0)
    {
        for (int i = 0; i < 500; ++i)
        {
            if (tmp[i] < 32)
                tmp[i] = 0;
        }
        Pname = findLeafName(filename);
        Pcomment = string(tmp);
        // loads the number of the notes
        if (loadLine(text, point, tmp, BUFFSIZ))
            err = SCALES::errors::badFile;
    }
    if (err == 0)
    {
        nnotes = MAX_OCTAVE_SIZE;
        sscanf(&tmp[0], "%d", &nnotes);
        if (size_t(nnotes) > MAX_OCTAVE_SIZE || nnotes < 2)
            err = SCALES::errors::badOctaveSize;
    }
    if (err == 0)
    {
    // load the tunings
        for (int nline = 0; nline < nnotes; ++nline)
        {
            err = loadLine(text, point, tmp, BUFFSIZ);
            if (err == 0)
                err = linetotunings(nline, &tmp[0]);
            if (err < 0)
                break;
        }
    }
    if (err < 0)
        return err;

    octavesize = nnotes;

    //synth->setAllPartMaps();
    synth->addHistory(filename, TOPLEVEL::XML::ScalaTune);
    return nnotes;
}


// Loads the mapping from a kbm file
int Microtonal::loadkbm(const string& filename)
{
    string text = loadText(filename);
    if (text == "")
        return SCALES::errors::noFile;
    char tmp[BUFFSIZ];
    size_t point = 0;
    int err = 0;
    int tmpMapSize;
    // loads the mapsize
    if (loadLine(text, point, tmp, BUFFSIZ))
        err = SCALES::errors::badFile;
    else if (!sscanf(&tmp[0], "%d",&tmpMapSize))
        err = SCALES::errors::badChars;

    if (err == 0)
    {
        if (tmpMapSize < 1 || tmpMapSize > 127)
            err = SCALES::errors::badMapSize;
    }

    int tmpFirst;
    if (err == 0)
    {
        // loads first MIDI note to retune
        if (loadLine(text, point, tmp, BUFFSIZ))
            err = SCALES::errors::badFile;
        else if (!sscanf(&tmp[0], "%d", &tmpFirst))
            return SCALES::errors::badOctaveSize;
        else if (tmpFirst < 0 || tmpFirst > 127)
            err = -7;
    }

    int tmpLast;
    if (err == 0)
    {
        // loads last MIDI note to retune
       if (loadLine(text, point, tmp, BUFFSIZ))
            err = SCALES::errors::badFile;
        else if (!sscanf(&tmp[0], "%d", &tmpLast))
            return SCALES::errors::badOctaveSize;
        else if (tmpLast < 0 || tmpLast > 127)
            err = SCALES::errors::badNoteNumber;
    }

    int tmpMid;
    if (err == 0)
    {
        // loads the middle note where scale from scale degree=0
       if (loadLine(text, point, tmp, BUFFSIZ))
            err = SCALES::errors::badFile;
        else if (!sscanf(&tmp[0], "%d", &tmpMid))
            return SCALES::errors::badMapSize;
        else if (tmpMid < 0 || tmpMid > 127)
            err = -SCALES::errors::badNoteNumber;
    }

    int tmpNote;
    if (err == 0)
    {
        // loads the reference note
       if (loadLine(text, point, tmp, BUFFSIZ))
            err = SCALES::errors::badFile;
        else if (!sscanf(&tmp[0], "%d", &tmpNote))
            return SCALES::errors::badOctaveSize;
        else if (tmpNote < 0 || tmpNote > 127)
            err = -7;
    }

    float tmpPrefFreq;
    if (err == 0)
    {
        // loads the reference freq.
        if (loadLine(text, point, tmp, BUFFSIZ))
            err = SCALES::errors::badOctaveSize;
        else
        {

            if (!sscanf(&tmp[0], "%f", &tmpPrefFreq))
                err = SCALES::errors::badOctaveSize;
            else if (tmpPrefFreq < 1 || tmpPrefFreq > 20000)
                err = SCALES::errors::outOfRange;
        }
    }

    // the scale degree(which is the octave) is not loaded
    // it is obtained by the tunings with getoctavesize() method
    if (loadLine(text, point, tmp, BUFFSIZ))
        err = SCALES::errors::badMapSize;

    int x;
    if (err == 0)
    {
        for (int nline = 0; nline < tmpMapSize; ++nline)
        {
            if (loadLine(text, point, tmp, BUFFSIZ))
            {
                err = SCALES::errors::missingEntry;
                break;
            }
            string alt = string (tmp);
            if (alt[0] < '0' || alt[0] > '9') // catches all possibilities!
            {
                x = -1;
            }
            else
            {
                x = std::stoi(alt);
                if (x >= tmpMapSize)
                {
                    err = SCALES::errors::valueTooBig;
                    break;
                }
            }
            Pmapping[nline] = x;
            size_t pos = alt.find('!');
            if (pos != std::string::npos)
                PmapComment[nline] = func::trimEnds(alt.substr(pos + 1, alt.length()));
        }
    }
    if (err < 0)
        return err;

    Pmappingenabled = 1;
    Pmapsize = tmpMapSize;
    Pfirstkey = tmpFirst;
    Plastkey = tmpLast;
    Pmiddlenote = tmpMid;
    PrefNote = tmpNote;
    PrefFreq = tmpPrefFreq;
    synth->setAllPartMaps();
    synth->addHistory(filename, TOPLEVEL::XML::ScalaMap);
    return tmpMapSize;
}


string Microtonal::scale2scl()
{
    string text = "! ";
    text += synth->microtonal.Pname;
    text += "\n!\n ";
    text += synth->microtonal.Pcomment;
    text += "\n ";
    text += to_string(synth->microtonal.octavesize);
    text += "\n!\n";
    for (size_t i = 0; i < synth->microtonal.octavesize; ++ i)
    {
        text += " ";
        if (octave[i].type == 1)
            text += synth->microtonal.octave[i].text;
        else
        {
            text+= to_string(octave[i].x1);
            text += "/";
            text+= to_string(octave[i].x2);
        }
        if (!octave[i].comment.empty())
        {
            text += " ! ";
            text += octave[i].comment;
        }
        text += "\n";
    }
    return text;
}

string Microtonal::map2kbm()
{
    string text = "! Scala keymap\n";
    text += "!\n";
//    text += "! map size\n";
    text += to_string(Pmapsize);
    text += "\n!\n";
//    text += "! first note\n";
    text += to_string(Pfirstkey);
    text += "\n!\n";
//    text += "! last note\n";
    text += to_string(Plastkey);
    text += "\n!\n";
//    text += "! middle note\n";
    text += to_string(Pmiddlenote);
    text += "\n!\n";
//    text += "! reference note\n";
    text += to_string(PrefNote);
    text += "\n!\n";
//    text += "! reference frequency\n";
    text += to_string(PrefFreq);
    text += "\n!\n";
//    text += "! formal octave\n";
    text += to_string(octavesize);
    text += "\n!\n";
    text += "! mapped notes\n";
    text += keymaptotext();
    text += "\n";
    return text;
}


void Microtonal::add2XML(XMLwrapper *xml)
{
    xml->addparstr("name", Pname.c_str());
    xml->addparstr("comment", Pcomment.c_str());

    xml->addparbool("invert_up_down", Pinvertupdown);
    xml->addpar("invert_up_down_center", Pinvertupdowncenter);

    xml->addparbool("enabled", Penabled);
    xml->addparcombi("global_fine_detune", Pglobalfinedetune);

    xml->addpar("a_note", PrefNote);
    xml->addparreal("a_freq", PrefFreq);

    if (!Penabled && xml->minimal)
        return;

    xml->beginbranch("SCALE");
        xml->addpar("scale_shift", Pscaleshift);
        xml->addpar("first_key", Pfirstkey);
        xml->addpar("last_key", Plastkey);
        xml->addpar("middle_note", Pmiddlenote);

        xml->beginbranch("OCTAVE");
        xml->addpar("octave_size", octavesize);
        for (size_t i = 0; i < octavesize; ++i)
        {
            xml->beginbranch("DEGREE", i);
            if (octave[i].type == 1)
            {
                xml->addparstr("cents_text",octave[i].text);
                xml->addparreal("cents", octave[i].tuning);
                /*
                 * This is downgraded to preserve compatibility
                 * with both Zyn and older Yoshi versions
                 */
            }
            if (octave[i].type == 2)
            {
                xml->addparstr("cents_text",octave[i].text);
                xml->addpar("numerator", octave[i].x1);
                xml->addpar("denominator", octave[i].x2);;
            }
            xml->addparstr("comment" , octave[i].comment);
            xml->endbranch();
        }
        xml->endbranch();

        xml->beginbranch("KEYBOARD_MAPPING");
        xml->addpar("map_size", Pmapsize);
        xml->addpar("mapping_enabled", Pmappingenabled);
        for (int i = 0; i < Pmapsize; ++i)
        {
            xml->beginbranch("KEYMAP", i);
            xml->addpar("degree", Pmapping[i]);
            xml->addparstr("comment", PmapComment[i]);
            xml->endbranch();
        }
        xml->endbranch();
    xml->endbranch();
}


int Microtonal::getfromXML(XMLwrapper *xml)
{
    int err = 0;
    Pname = xml->getparstr("name");
    Pcomment = xml->getparstr("comment");

    Pinvertupdown=xml->getparbool("invert_up_down", Pinvertupdown);
    Pinvertupdowncenter=xml->getpar127("invert_up_down_center", Pinvertupdowncenter);

    Penabled=xml->getparbool("enabled", Penabled);
    Pglobalfinedetune = xml->getparcombi("global_fine_detune", Pglobalfinedetune, 0, 127);

    PrefNote = xml->getpar127("a_note", PrefNote);
    PrefFreq = xml->getparreal("a_freq", PrefFreq, 1.0, 10000.0);

    if (xml->enterbranch("SCALE"))
    {
        Pscaleshift = xml->getpar127("scale_shift", Pscaleshift);
        Pfirstkey = xml->getpar127("first_key", Pfirstkey);
        Plastkey = xml->getpar127("last_key", Plastkey);
        Pmiddlenote = xml->getpar127("middle_note", Pmiddlenote);

        if (xml->enterbranch("OCTAVE"))
        {
            octavesize = xml->getpar127("octave_size", octavesize);
            for (size_t i = 0; i < octavesize; ++i)
            {
                octave[i].text = "";
                if (!xml->enterbranch("DEGREE", i))
                    continue;
                string text = xml->getparstr("cents_text");
                octave[i].x2 = 0;
                if (text > " ")
                {
                    octave[i].text = reformatline(text);
                    octave[i].tuning = pow(2.0, stod(text) / 1200.0);
                }
                else
                {
                    octave[i].text = "";
                    octave[i].tuning = xml->getparreal("cents", octave[i].tuning);
                }
                octave[i].x1 = xml->getpar("numerator", octave[i].x1, 0, INT_MAX);
                octave[i].x2 = xml->getpar("denominator", octave[i].x2, 0, INT_MAX);

                if (octave[i].x2)
                {
                    octave[i].text = text;
                    octave[i].type = 2;
                    octave[i].tuning = ((double)octave[i].x1) / octave[i].x2;
                }
                else {
                    octave[i].type = 1;
                    //populate fields for display
                    double x = log(octave[i].tuning) / LOG_2 * 1200.0;
                    octave[i].x1 = (int) floor(x);
                    octave[i].x2 = (int) (floor(fmod(x, 1.0) * 1e6));
                }
                octave[i].comment = "";
                octave[i].comment = xml->getparstr("comment");
                xml->exitbranch();
            }
            xml->exitbranch();
        }

        if (xml->enterbranch("KEYBOARD_MAPPING"))
        {
            Pmapsize = xml->getpar127("map_size", Pmapsize);
            Pmappingenabled = xml->getpar127("mapping_enabled", Pmappingenabled);
            for (int i = 0; i < Pmapsize; ++i)
            {
                if (!xml->enterbranch("KEYMAP", i))
                    continue;
                Pmapping[i] = xml->getpar("degree", Pmapping[i], -1, 127);
                PmapComment[i] = xml->getparstr("comment");
                if (Pmapping[i] >= Pmapsize)
                {
                    err = SCALES::errors::valueTooBig;
                    break;
                }
                xml->exitbranch();
            }
            xml->exitbranch();
        }
        xml->exitbranch();
    }
    return err;
}


bool Microtonal::saveXML(const string& filename)
{
    synth->getRuntime().xmlType = TOPLEVEL::XML::Scale;
    XMLwrapper *xml = new XMLwrapper(synth);

    xml->beginbranch("MICROTONAL");
    add2XML(xml);
    xml->endbranch();

    bool result = xml->saveXMLfile(filename);
    delete xml;
    return result;
}


int Microtonal::loadXML(const string& filename)
{
    int err = 0;
    XMLwrapper *xml = new XMLwrapper(synth);
    if (NULL == xml)
    {
        synth->getRuntime().Log("Microtonal: loadXML failed to instantiate new XMLwrapper", _SYS_::LogError);
        return 1;
    }
    if (!xml->loadXMLfile(filename))
    {
        delete xml;
        return 1;
    }
    if (!xml->enterbranch("MICROTONAL"))
    {
        synth->getRuntime().Log(filename + " is not a scale file", _SYS_::LogError);
        delete xml;
        return 1;
    }
    err = getfromXML(xml);
    if (err != 0)
    {
        delete xml;
        return err;
    }
    synth->setAllPartMaps();
    xml->exitbranch();
    delete xml;
    return 0;
}

float Microtonal::getLimits(CommandBlock *getData)
{
    float value = getData->data.value;
    int request = int(getData->data.type & TOPLEVEL::type::Default);
    int control = getData->data.control;

    unsigned char type = 0;

    // microtonal defaults
    int min = 0;
    float def = 0;
    int max = 127;
    type |= TOPLEVEL::type::Integer;
    unsigned char learnable = TOPLEVEL::type::Learnable;

    switch (control)
    {
        case SCALES::control::refFrequency:
            min = A_MIN;
            def = A_DEF;
            max = A_MAX;
            break;
        case SCALES::control::refNote:
            min = 24;
            def = 69;
            max = 84;
            type |= learnable;
            break;
        case SCALES::control::invertScale:
            max = 1;
            type |= learnable;
            break;
        case SCALES::control::invertedScaleCenter:
            def = 60;
            type |= learnable;
            break;
        case SCALES::control::scaleShift:
            min = -63;
            max = 64;
            type |= learnable;
            break;

        case SCALES::control::enableMicrotonal:
            max = 1;
            type |= learnable;
            break;

        case SCALES::control::enableKeyboardMap:
            max = 1;
            type |= learnable;
            break;
        case SCALES::control::lowKey:
            type |= learnable;
            break;
        case SCALES::control::middleKey:
            def = 60;
            type |= learnable;
            break;
        case SCALES::control::highKey:
            def = 127;
            type |= learnable;
            break;

        case SCALES::control::tuning:
            max = 1;
            break;
        case SCALES::control::keyboardMap:
            max = 1;
            break;
        case SCALES::control::importScl:
            max = 1;
            break;
        case SCALES::control::importKbm:
            max = 1;
            break;
        case SCALES::control::name:
            max = 1;
            break;
        case SCALES::control::comment:
            max = 1;
            break;
        case SCALES::control::retune:
            max = 1;
            break;
        case SCALES::control::clearAll:
            max = 1;
            break;

        default:
            type |= TOPLEVEL::type::Error;
            break;
    }
    getData->data.type = type;
    if (type & TOPLEVEL::type::Error)
        return 1;

    switch (request)
    {
        case TOPLEVEL::type::Adjust:
            if (value < min)
                value = min;
            else if (value > max)
                value = max;
        break;
        case TOPLEVEL::type::Minimum:
            value = min;
            break;
        case TOPLEVEL::type::Maximum:
            value = max;
            break;
        case TOPLEVEL::type::Default:
            value = def;
            break;
    }
    return value;
}
