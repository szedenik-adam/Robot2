/*
    \file GLFont.h

    This Code is covered under the LGPL.  See COPYING file for the license.

    $Id: GLFont.h 183 2010-07-18 15:20:20Z effer $
 */

#ifndef __GL_FONT_H__
#define __GL_FONT_H__

#ifdef HAVE_APPLE_OPENGL_FRAMEWORK
#    include <OpenGL/gl.h>
#    include <OpenGL/glu.h>
#    include <GLUT/glut.h>
#else
#    ifdef _WIN32
#      define _WINSOCKAPI_
#      include <windows.h>
#    endif
#    include <GL/gl.h>
#    include <GL/glu.h> 
//#    include <GL/glut.h>
#endif


#include <assert.h>
#include <stdio.h>

#include <cstdarg>
#include <string>
#include <string.h>


#define MAX_TEXT_LENGTH 512

extern "C" void glutBitmapCharacter_(void* fontID, int character);
#include <GL/freeglut.h>
#include "freeglut_internal.h"

 ///
class GLFont
{
public:
    GLFont()
    {
        m_nNumLists = 96;
        m_nCharWidth = 8;
        m_nCharHeight = 13;
        m_bInitDone = false;
    }
    ~GLFont();

    // printf style function take position to print to as well
    // NB: coordinates start from bottom left
    void glPrintf(int x, int y, const char* fmt, ...);
    void glPrintf(int x, int y, const std::string fmt, ...) { glPrintf(x, y, fmt.c_str()); }
    void glPrintfFast(int x, int y, const char* fmt, ...);
    void glPrintfFast(int x, int y, const std::string fmt, ...) { glPrintfFast(x, y, fmt.c_str()); }
    void PrintSingleLine(int x, int y, const char* text, int textLen = -1);
    void Print(int x, int y, const char* text);

    unsigned int   CharWidth() { return m_nCharWidth; }
    unsigned int   CharHeight() { return m_nCharHeight; }

private:
    unsigned int   m_nCharWidth; // fixed width
    unsigned int   m_nCharHeight; // fixed width
    int            m_nNumLists;        // number of display lists
    int            m_nDisplayListBase; // base number for display lists
    bool           m_bInitDone;

    bool CheckInit();
};

////////////////////////////////////////////////////////////////////////////////
///
inline bool GLFont::CheckInit()
{
    // make sure glutInit has been called
    /*if (glutGet(GLUT_ELAPSED_TIME) <= 0) {
        //fprintf( stderr, "WARNING: GLFontCheckInit failed after 'glutGet(GLUT_ELAPSED_TIME) <= 0' check\n" );
        return false;
    }*/

    // GLUT bitmapped fonts...  
    this->m_nDisplayListBase = glGenLists(this->m_nNumLists);
    if (this->m_nDisplayListBase == 0) {
        //    hmm, commented out for now because on my linux box w get here sometimes
        //    even though glut hasn't been initialized.
        //            fprintf( stderr, "%i", pFont->m_nNumLists );
        fprintf(stderr, "GLFontCheckInit() -- out of display lists\n");
        return false;
    }
    for (int nList = this->m_nDisplayListBase;
        nList < this->m_nDisplayListBase + this->m_nNumLists; nList++) {
        glNewList(nList, GL_COMPILE);
        glutBitmapCharacter_(GLUT_BITMAP_8_BY_13, nList + 32 - this->m_nDisplayListBase);
        glEndList();
    }

    this->m_bInitDone = true;
    return true;
}

////////////////////////////////////////////////////////////////////////////////
inline GLFont::~GLFont()
{
    if (m_bInitDone && this->CheckInit()) {
        glDeleteLists(m_nDisplayListBase, m_nDisplayListBase + m_nNumLists);
        m_bInitDone = false;
    }
}

////////////////////////////////////////////////////////////////////////////////
// printf style print function
// NB: coordinates start from bottom left
inline void GLFont::glPrintf(int x, int y, const char* fmt, ...)
{
    this->CheckInit();

    char        text[MAX_TEXT_LENGTH];                  // Holds Our String
    va_list     ap;                                     // Pointer To List Of Arguments

    if (fmt == NULL) {                                 // If There's No Text
        return;                                         // Do Nothing
    }

    va_start(ap, fmt);                                // Parses The String For Variables
    vsnprintf(text, MAX_TEXT_LENGTH, fmt, ap);         // And Converts Symbols To Actual Numbers
    va_end(ap);                                       // Results Are Stored In Text

    // This saves our transform (matrix) information and our current viewport information.
    // Also save depth test enable bit.
    glPushAttrib(GL_TRANSFORM_BIT | GL_VIEWPORT_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST); //causes text not to clip with geometry
    //position text correctly...

    // Use a new projection and modelview matrix to work with.
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    //create a viewport at x,y, but doesnt have any width (so we end up drawing there...)
    glViewport(x - 1, y - 1, 0, 0);
    //This actually positions the text.
    glRasterPos4f(0, 0, 0, 1);
    //undo everything
    glPopMatrix();                                      // Pop the current modelview matrix off the stack
    glMatrixMode(GL_PROJECTION);                      // Go back into projection mode
    glPopMatrix();                                      // Pop the projection matrix off the stack
    glPopAttrib();                                      // This restores our TRANSFORM and VIEWPORT attributes + GL_DEPTH_TEST

    //glRasterPos2f(x, y);

    glPushAttrib(GL_LIST_BIT);                        // Pushes The Display List Bits
    glListBase(m_nDisplayListBase - 32);      // Sets The Base Character to 32
    //glScalef( 0.5, 0.5, 0.5 ); 
    glCallLists(strlen(text), GL_UNSIGNED_BYTE, text);// Draws The Display List Text
    glPopAttrib();                                      // Pops The Display List Bits
    //glEnable(GL_DEPTH_TEST); // 
}

////////////////////////////////////////////////////////////////////////////////
//printf style print function
//NOTE: coordinates start from bottom left
//ASSUMES ORTHOGRAPHIC PROJECTION ALREADY SET UP...
inline void GLFont::glPrintfFast(int x, int y, const char* fmt, ...)
{
    this->CheckInit();

    char        text[MAX_TEXT_LENGTH];// Holds Our String
    va_list     ap;                   // Pointer To List Of Arguments

    if (fmt == NULL) {               // If There's No Text
        return;                       // Do Nothing
    }

    va_start(ap, fmt);                            // Parses The String For Variables
    int textLen = vsnprintf(text, MAX_TEXT_LENGTH, fmt, ap);    // And Converts Symbols To Actual Numbers
    va_end(ap);                                   // Results Are Stored In Text

    this->PrintSingleLine(x, y, text, textLen);
}

inline void GLFont::PrintSingleLine(int x, int y, const char* text, int textLen)
{
    if (!textLen) { return; }
    if (textLen == -1) { textLen = strlen(text); }

    glDisable(GL_DEPTH_TEST); // Causes text not to clip with geometry
    glRasterPos2f(x, y);
    //glPushAttrib( GL_LIST_BIT );                        // Pushes The Display List Bits
    glListBase(m_nDisplayListBase - 32);        // Sets The Base Character to 32
    glCallLists(textLen, GL_UNSIGNED_BYTE, text);  // Draws The Display List Text
    //glPopAttrib();                                      // Pops The Display List Bits
    //glEnable(GL_DEPTH_TEST); 
}

inline void GLFont::Print(int x, int y, const char* text)
{
    size_t remainingLen = strlen(text);
    while (true)
    {
        const char* newLinePtr = strchr(text, '\n');
        size_t writeLen = (newLinePtr) ? (newLinePtr - text) : remainingLen;
        this->PrintSingleLine(x, y, text, writeLen);

        if (newLinePtr)
        {
            remainingLen -= writeLen + 1;
            text += writeLen + 1;
            y += this->m_nCharHeight+1;
        }
        else { break; }
    }
}

#endif
