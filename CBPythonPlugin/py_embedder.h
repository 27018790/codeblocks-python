#ifndef PYEMBEDDER_H_INCLUDED
#define PYEMBEDDER_H_INCLUDED

#include <wx/wx.h>
#include <wx/app.h>
#include <wx/dynarray.h>
#include <wx/process.h>

//#include <memory>
#include <iostream>
#include "XmlRpc.h"

class PyJob;
class PyInstance;

/////////////////////////////////////////////////////////////////////////////////////
// Python Interpreter Events
/////////////////////////////////////////////////////////////////////////////////////

BEGIN_DECLARE_EVENT_TYPES()
DECLARE_EVENT_TYPE(wxEVT_PY_NOTIFY_INTERPRETER, -1)
DECLARE_EVENT_TYPE(wxEVT_PY_NOTIFY_UI, -1)
END_DECLARE_EVENT_TYPES()

int ID_PY_PROC=wxNewId();


// Events sent from the UI to an intepreter request for shutdown
class PyNotifyIntepreterEvent: public wxEvent
{
public:
    PyNotifyIntepreterEvent(int id);
    PyNotifyIntepreterEvent(const PyNotifyIntepreterEvent& c) : wxEvent(c) { }
    wxEvent *Clone() const { return new PyNotifyIntepreterEvent(*this); }
    ~PyNotifyIntepreterEvent() {}
};

enum JobStates {PYSTATE_STARTEDJOB, PYSTATE_FINISHEDJOB, PYSTATE_ABORTEDJOB};

// Events sent from the thread interacting with the python interpreter back to the UI.
// indicating job completion, interpreter shutdown etc
class PyNotifyUIEvent: public wxEvent
{
    friend class PyInstance;
public:
    PyNotifyUIEvent(int id, PyInstance *instance, JobStates jobstate);
    PyNotifyUIEvent(const PyNotifyUIEvent& c) : wxEvent(c)
    {
        jobstate = c.jobstate;
        instance= c.instance;
    }
    wxEvent *Clone() const { return new PyNotifyUIEvent(*this); }
    ~PyNotifyUIEvent() {}
    PyJob *GetJob() {return job;}
    PyInstance *GetInterpreter();
    void SetState(JobStates s) {jobstate=s;}
    JobStates jobstate;
protected:
    PyInstance *instance;
    PyJob *job;
    wxWindow *parent;
};

typedef void (wxEvtHandler::*PyNotifyIntepreterEventFunction)(PyNotifyIntepreterEvent&);
typedef void (wxEvtHandler::*PyNotifyUIEventFunction)(PyNotifyUIEvent&);

#define EVT_PY_NOTIFY_INTERPRETER(id, fn) \
    DECLARE_EVENT_TABLE_ENTRY( wxEVT_PY_NOTIFY_INTERPRETER, id, -1, \
    (wxObjectEventFunction) (wxEventFunction) \
    wxStaticCastEvent( PyNotifyIntepreterEventFunction, & fn ), (wxObject *) NULL ),

#define EVT_PY_NOTIFY_UI(id, fn) \
    DECLARE_EVENT_TABLE_ENTRY( wxEVT_PY_NOTIFY_UI, id, -1, \
    (wxObjectEventFunction) (wxEventFunction) \
    wxStaticCastEvent( PyNotifyUIEventFunction, & fn ), (wxObject *) NULL ),


//////////////////////////////////////////////////////
// PyJob: An abstract class for a python job to be
// run by an interpreter instance. The job is defined
// in the pure virtual method operator(). The job
// should try to restrict itself to writing to its
// own non-GUI data members for thread safety.
//////////////////////////////////////////////////////
class PyJob: public wxThread
{
public:
    PyJob(PyInstance *pyinst, wxWindow *p, int id, bool selfdestroy=true);
    virtual ~PyJob();
    bool operator()() {return false;}
protected:
    virtual void *Entry();
    PyInstance *pyinst;
    wxWindow *parent;
    int id;
    bool finished;
    bool started;
    bool killonexit;
    friend class PyInstance;
};

WX_DECLARE_LIST(PyJob, PyJobQueue);

/////////////////////////////////////////////////////////////////////////////////////
// PyInstance: The interface to an instance of a running interpreter
// each instantance launches an external python process then
// connects via some sort of socket interface (xml-rpc currently)
// The interface maintains a queue of jobs for the interpreter,
// which are run in sequence as worker threads. a job is a single/multiple
// xml-rpc method request
// jobs must not interact with objects, esp. gui objects, on the main thread.
/////////////////////////////////////////////////////////////////////////////////////
class PyInstance: public wxEvtHandler
{
public:
    PyInstance(const wxString &hostaddress, int port)
    {
      m_port=port;
      m_hostaddress=hostaddress;
      // Use introspection API to look up the supported methods
      m_client = new XmlRpc::XmlRpcClient(hostaddress.char_str(), port);
      XmlRpc::XmlRpcValue noArgs, result;
      if (m_client->execute("system.listMethods", noArgs, result))
        std::cout << "\nMethods:\n " << result << "\n\n";
      else
        std::cout << "Error calling 'listMethods'\n\n";
    }

    long LaunchProcess(wxString processcmd, bool ParseLinks, bool LinkClicks, const wxString &LinkRegex)
    {
        if(!m_proc_dead)
            return -1;
        if(m_proc) //this should never happen
            m_proc->Detach(); //self cleanup
        m_proc=new wxProcess(this,ID_PY_PROC);
        m_proc->Redirect();
        m_proc_id=wxExecute(processcmd,wxEXEC_ASYNC,m_proc);
        if(m_proc_id>0)
        {
            m_proc_dead=false;
            m_proc_killlevel=0;
        }
        return m_proc_id;
    }
    PyInstance(const PyInstance &copy)
    {
        m_paused=copy.m_paused;
        m_queue=copy.m_queue;
        m_hostaddress=copy.m_hostaddress;
        m_port=copy.m_port;
    }
    ~PyInstance();
    void EvalString(char *str, bool wait=true);
    long GetPid() {if(m_proc) return m_proc_id; else return -1;}
    void KillProcess(bool force=false)
    {
        if(m_proc_dead)
            return;
        long pid=GetPid();
        if(m_proc_killlevel==0)
        {
            m_proc_killlevel=1;
            if(wxProcess::Exists(pid))
                wxProcess::Kill(pid,wxSIGTERM);
            return;
        }
        if(m_proc_killlevel==1)
        {
            if(wxProcess::Exists(pid))
            {
//                cbMessageBox(_T("Forcing..."));
                wxProcess::Kill(pid,wxSIGKILL);
            }
        }
    }
    bool AddJob(PyJob *job);
    void OnJobNotify(PyNotifyUIEvent &event);
    void PauseJobs();
    void ClearJobs();
private:
    wxProcess *m_proc; // external python process
    long m_proc_id;
    int  m_proc_killlevel;
    bool m_proc_dead;
    PyJobQueue m_queue;
    bool m_paused; //if paused is true, new jobs in the queue will not be processed automatically
    wxString m_hostaddress; //address for python server process
    int m_port; // port number for server
    XmlRpc::XmlRpcClient *m_client;
    //void AttachExtension(); //attach a python extension table as an import for this interpreter
    DECLARE_EVENT_TABLE();
};

WX_DECLARE_OBJARRAY(PyInstance, PyInstanceCollection);

/////////////////////////////////////////////////////////////////////////////////////
// PyMgr: manages the collection of interpreters
/////////////////////////////////////////////////////////////////////////////////////
class PyMgr
{
public:
    PyInstance *LaunchInterpreter();
    static PyMgr &Get();
    ~PyMgr();
protected:
    PyMgr();
private:
    PyInstanceCollection m_Interpreters;
    static std::auto_ptr<PyMgr> theSingleInstance;
// todo: create an xmlrpc server?

};


#endif //PYEMBEDDER_H_INCLUDED
