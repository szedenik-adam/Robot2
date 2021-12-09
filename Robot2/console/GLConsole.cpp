
#include <GL/glew.h>
#include <GL/GL.h>
#include "GLConsole.h"
#include "../Environment.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Constructor
 */
GLConsole::GLConsole(SDL_Window& window) :
    // Init our member cvars  (can't init the names in the class decleration)
    m_fConsoleBlinkRate(CVarUtils::CreateCVar<float>("console.BlinkRate", 4.0)), // cursor blinks per sec
    m_fConsoleAnimTime(CVarUtils::CreateCVar<float>("console.AnimTime", 0.1)),     // time the console animates
    m_nConsoleMaxHistory(CVarUtils::CreateCVar<int>("console.history.MaxHistory", 100)), // max lines ofconsole history
    m_nConsoleLineSpacing(CVarUtils::CreateCVar<int>("console.LineSpacing", 2)), // pixels between lines
    m_nConsoleLeftMargin(CVarUtils::CreateCVar<int>("console.LeftMargin", 5)),   // left margin in pixels
    m_nConsoleVerticalMargin(CVarUtils::CreateCVar<int>("console.VertMargin", 8)),
    m_nConsoleMaxLines(CVarUtils::CreateCVar<int>("console.MaxLines", 200)),
    m_fOverlayPercent(CVarUtils::CreateCVar<float>("console.OverlayPercent", 0.75)),
    m_sHistoryFileName(CVarUtils::CreateCVar<>("console.history.HistoryFileName", std::string(GLCONSOLE_HISTORY_FILE))),
    m_sScriptFileName(CVarUtils::CreateCVar<>("script.ScriptFileName", std::string(GLCONSOLE_SCRIPT_FILE))),
    m_sSettingsFileName(CVarUtils::CreateCVar<>("console.settings.SettingsFileName", std::string(GLCONSOLE_SETTINGS_FILE))),
    m_sInitialScriptFileName(CVarUtils::CreateCVar<>("console.InitialScriptFileName", std::string(GLCONSOLE_INITIAL_SCRIPT_FILE))),
    commands(),
    m_logColor(CVarUtils::CreateCVar<CVarUtils::Color>("console.colors.LogColor", CVarUtils::Color(255, 255, 64))),
    m_commandColor(CVarUtils::CreateCVar<CVarUtils::Color>("console.colors.CommandColor", CVarUtils::Color(255, 255, 255))),
    m_functionColor(CVarUtils::CreateCVar<CVarUtils::Color>("console.colors.FunctionColor", CVarUtils::Color(64, 255, 64))),
    m_errorColor(CVarUtils::CreateCVar<CVarUtils::Color>("console.colors.ErrorColor", CVarUtils::Color(255, 128, 64))),
    m_helpColor(CVarUtils::CreateCVar<CVarUtils::Color>("console.colors.HelpColor", CVarUtils::Color(110, 130, 200))),
    m_consoleColor(CVarUtils::CreateCVar<CVarUtils::Color>("console.colors.ConsoleColor", CVarUtils::Color(25, 60, 130, 120))),
    window(window)
{

    SetConsole(this);

    //pConsole = this;
    m_Viewport.width = 0;
    m_bConsoleOpen = 0;
    m_bSavingScript = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Render the console
 */
void GLConsole::RenderConsole()
{
    _CheckInit();
    if (m_bConsoleOpen || m_bIsChanging) {
        glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_SCISSOR_BIT | GL_TRANSFORM_BIT | GL_CURRENT_BIT);

        glDisable(GL_LIGHTING);


        //get the width and heigtht of the viewport
        glGetIntegerv(GL_VIEWPORT, &m_Viewport.x);

        //reset matrices and switch to ortho view
        glDisable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0, m_Viewport.width, 0, m_Viewport.height, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        //set up a scissor region to draw the console in
        GLfloat bottom = m_Viewport.height - _GetConsoleHeight();
        glScissor(0, bottom, //bottom coord
            m_Viewport.width, //width
            m_Viewport.height); //top coord
        glEnable(GL_SCISSOR_TEST);

        //render transparent background
        glDisable(GL_DEPTH_TEST); //for transparency
        //blend function based on source alpha
        glEnable(GL_BLEND);
        //glBlendFunc(GL_ONE, GL_ZERO);
        //glBlendFunc(GL_DST_ALPHA, GL_SRC_COLOR);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.1, 0.1, 0.1, 0.6);
        glRectf(0, bottom, m_Viewport.width, m_Viewport.height);
        
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
        glColor4f(m_consoleColor.r,
            m_consoleColor.g,
            m_consoleColor.b,
            m_consoleColor.a);

        GLfloat verts[] = { 0.0f, bottom,
                            (GLfloat)m_Viewport.width, bottom,
                            (GLfloat)m_Viewport.width, (GLfloat)m_Viewport.height,
                            0.0f, (GLfloat)m_Viewport.height };
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDisableClientState(GL_VERTEX_ARRAY);

        //draw text
        _RenderText();

        //restore old matrices and properties...
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();

        glPopAttrib();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * Process key presses
 */
void GLConsole::KeyboardFunc(unsigned char key, uint16_t modifiers)
{
    if (!IsOpen())
        return;

    _CheckInit();

    //int nGlutModifiers = 0;glutGetModifiers();
    //   int ikey = key;
    //   std::cerr << "kf: key: " << key << ", " << ikey << " mod: " << nGlutModifiers << std::endl;

    switch (modifiers)
    {
    case GLUT_ACTIVE_CTRL:
        //Yay for GLUT. when control is active the key codes change for some but not all keys, and appears not to be documented.
        //a-z and A-Z are now 1-26 and not 97-122 and 65-90
        switch (key) {
        case ('a' - 96):
            CursorToBeginningOfLine();
            break;
        case ('e' - 96):
            CursorToEndOfLine();
            break;
        case('c' - 96):
            ClearCurrentCommand();
            break;
        case('w' - 96):
            ClearCurrentWord();
            break;
        default:
            break;
        }
        break;

    case GLUT_ACTIVE_ALT:

        break;

    case GLUT_ACTIVE_SHIFT:
    default:
        switch (key)
        {
        case '\r':
            //user pressed "enter"
            _ProcessCurrentCommand();
            m_sCurrentCommandBeg = "";
            m_sCurrentCommandEnd = "";
            m_nCommandNum = 0; //reset history
            m_nScrollPixels = 0; //reset scrolling
            break;

        case '\t':
            //tab complete
            _TabComplete();
            break;

        case '\b':
            // backspace
            if (m_sCurrentCommandBeg.size() > 0) {
                m_sCurrentCommandBeg = m_sCurrentCommandBeg.substr(0, m_sCurrentCommandBeg.size() - 1);
            }
            break;

        case CVAR_DEL_KEY:
            // delete
            if (m_sCurrentCommandEnd.size() > 0) {
                m_sCurrentCommandEnd = m_sCurrentCommandEnd.substr(1, m_sCurrentCommandEnd.size());
            }
            break;
#if 0
        }
    else if (chr == ' ' && m_sCurrentCommandEnd.size() > 0) { // space clear the right bit of the cursor
        m_sCurrentCommandEnd = "";
#endif
        default:
            m_sCurrentCommandBeg += key;
            m_nCommandNum = 0; //reset history
            m_nScrollPixels = 0; //reset scrolling
    }
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * What to do when certain special keys are pressed
 * such as arrow keys, home, end, page up, page down
 *
 */
void GLConsole::SpecialFunc(int key, uint16_t modifiers)
{
    if (!IsOpen())
        return;

    _CheckInit();

    //int nGlutModifiers = glutGetModifiers();

    //   unsigned char ckey = key;
    //   std::cerr << "sf: key: " << ckey << ", " << key << " mod: " << nGlutModifiers << std::endl;


    switch (modifiers)
    {
    case GLUT_ACTIVE_SHIFT:
        switch (key) {
        case SDL_SCANCODE_UP:
            ScrollDownLine();
            break;
        case SDL_SCANCODE_DOWN:
            ScrollUpLine();
            break;
        default:
            break;
        }
        break;

    case GLUT_ACTIVE_CTRL:
        switch (key) {
        case SDL_SCANCODE_LEFT:
            CursorToBeginningOfLine();
            break;
        case SDL_SCANCODE_RIGHT:
            CursorToEndOfLine();
            break;
        }
        break;

    case GLUT_ACTIVE_ALT:
        switch (key) {
        case SDL_SCANCODE_LEFT:
            ClearCurrentWord();
            break;
        }
        break;

    default:
        switch (key) {
        case SDL_SCANCODE_LEFT:
            CursorLeft();
            break;
        case SDL_SCANCODE_RIGHT:
            CursorRight();
            break;
        case SDL_SCANCODE_PAGEUP:
            ScrollDownPage();
            break;
        case SDL_SCANCODE_PAGEDOWN:
            ScrollUpPage();
            break;
        case SDL_SCANCODE_UP:
            HistoryBack();
            break;
        case SDL_SCANCODE_DOWN:
            HistoryForward();
            break;
        case SDL_SCANCODE_HOME:
            CursorToBeginningOfLine();
            break;
        case SDL_SCANCODE_END:
            CursorToEndOfLine();
            break;
        default:
            break;
        }
    }
}



////////////////////////////////////////////////////////////////////////////////
// commands are in the following form:
// [Command] = value //sets a value
// or
// [Command] //prints out the command's value
bool GLConsole::_ProcessCurrentCommand(bool bExecute)
{
    //trie version
    //int error = 0;
    TrieNode* node;
    std::string sRes;
    bool bSuccess = true;

    Trie& trie = CVarUtils::TrieInstance();
    std::string m_sCurrentCommand = m_sCurrentCommandBeg + m_sCurrentCommandEnd;

    // remove leading and trailing spaces
    int pos = m_sCurrentCommand.find_first_not_of(" ", 0);
    if (pos >= 0) {
        m_sCurrentCommand = m_sCurrentCommand.substr(pos);
    }
    pos = m_sCurrentCommand.find_last_not_of(" ");
    if (pos >= 0) {
        m_sCurrentCommand = m_sCurrentCommand.substr(0, pos + 1);
    }

    // Make sure the command gets added to the history
    if (!m_sCurrentCommand.empty()) {
        EnterLogLine(m_sCurrentCommand.c_str(), LINEPROP_COMMAND, false);
    }

    // Simply print value if the command is just a variable
    if ((node = trie.Find(m_sCurrentCommand))) {
        //execute function if this is a function cvar
        if (_IsConsoleFunc(node)) {
            bSuccess &= CVarUtils::ExecuteFunction(m_sCurrentCommand, (CVarUtils::CVar<ConsoleFunc>*) node->m_pNodeData, sRes, bExecute);
            EnterLogLine(m_sCurrentCommand.c_str(), LINEPROP_FUNCTION);
        }
        else { //print value associated with this cvar
            EnterLogLine((m_sCurrentCommand + " = " +
                CVarUtils::GetValueAsString(node->m_pNodeData)).c_str(), LINEPROP_LOG);
        }
    }
    //see if it is an assignment or a function execution (with arguments)
    else {
        int eq_pos; //get the position of the equal sign
        //see if this an assignment
        if ((eq_pos = m_sCurrentCommand.find("=")) != -1) {
            std::string command, value;
            std::string tmp = m_sCurrentCommand.substr(0, eq_pos);
            command = RemoveSpaces(tmp);
            value = m_sCurrentCommand.substr(eq_pos + 1, m_sCurrentCommand.length());
            if (!value.empty()) {
                value = RemoveSpaces(value);
                if ((node = trie.Find(command))) {
                    if (bExecute) {
                        CVarUtils::SetValueFromString(node->m_pNodeData, value);
                    }
                    EnterLogLine((command + " = " + value).c_str(), LINEPROP_LOG);
                }
            }
            else {
                if (bExecute) {
                    std::string out = command + ": command not found";
                    EnterLogLine(out.c_str(), LINEPROP_ERROR);
                }
                bSuccess = false;
            }
        }
        //check if this is a function
        else if ((eq_pos = m_sCurrentCommand.find(" ")) != -1) {
            std::string function, params;
            function = m_sCurrentCommand.substr(0, eq_pos);
            params = m_sCurrentCommand.substr(eq_pos+1);

            bSuccess = commands.Execute(function, params);
            EnterLogLine(m_sCurrentCommand.c_str(), LINEPROP_FUNCTION);

            /*//check if this is a valid function name
            if ((node = trie.Find(function)) && _IsConsoleFunc(node)) {
                bSuccess &= CVarUtils::ExecuteFunction(m_sCurrentCommand, (CVarUtils::CVar<ConsoleFunc>*)node->m_pNodeData, sRes, bExecute);
                EnterLogLine(m_sCurrentCommand.c_str(), LINEPROP_FUNCTION);
            }
            else {
                if (bExecute) {
                    std::string out = function + ": function not found";
                    EnterLogLine(out.c_str(), LINEPROP_ERROR);
                }
                bSuccess = false;
            }*/
        }
        else if (!m_sCurrentCommand.empty()) {
            if (bExecute) {
                std::string out = m_sCurrentCommand + ": command not found";
                EnterLogLine(out.c_str(), LINEPROP_ERROR);
            }
            bSuccess = false;
        }
        else { // just pressed enter
            EnterLogLine(" ", LINEPROP_LOG);
        }
    }

    return bSuccess;
}