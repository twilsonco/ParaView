/*=========================================================================

  Program:   ParaView
  Module:    vtkPVSource.cxx

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkPVSource.h"

#include "vtkArrayMap.txx"
#include "vtkCollection.h"
#include "vtkCollectionIterator.h"
#include "vtkDataSet.h"
#include "vtkKWEntry.h"
#include "vtkKWFrame.h"
#include "vtkKWLabel.h"
#include "vtkKWLabeledEntry.h"
#include "vtkKWLabeledFrame.h"
#include "vtkKWLabeledLabel.h"
#include "vtkKWMenu.h"
#include "vtkKWMessageDialog.h"
#include "vtkKWNotebook.h"
#include "vtkKWPushButton.h"
#include "vtkKWTkUtilities.h"
#include "vtkKWView.h"
#include "vtkObjectFactory.h"
#include "vtkPVApplication.h"
#include "vtkPVData.h"
#include "vtkPVDataInformation.h"
#include "vtkPVInputMenu.h"
#include "vtkSMPart.h"
#include "vtkPVPartDisplay.h"
#include "vtkPVInputProperty.h"
#include "vtkPVNumberOfOutputsInformation.h"
#include "vtkPVProcessModule.h"
#include "vtkPVRenderView.h"
#include "vtkPVRenderModule.h"
#include "vtkPVSourceCollection.h"
#include "vtkPVWidgetCollection.h"
#include "vtkPVWindow.h"
#include "vtkSMInputProperty.h"
#include "vtkSMPart.h"
#include "vtkSMPropertyIterator.h"
#include "vtkSMProxyManager.h"
#include "vtkSMSourceProxy.h"
#include "vtkSMProxyProperty.h"
#include "vtkSource.h"
#include "vtkString.h"
#include "vtkRenderer.h"
#include "vtkArrayMap.txx"
#include "vtkStringList.h"
#include "vtkPVAnimationInterface.h"
#include <vtkstd/vector>

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkPVSource);
vtkCxxRevisionMacro(vtkPVSource, "1.376");


int vtkPVSourceCommand(ClientData cd, Tcl_Interp *interp,
                           int argc, char *argv[]);

vtkCxxSetObjectMacro(vtkPVSource, View, vtkKWView);
vtkCxxSetObjectMacro(vtkPVSource, Proxy, vtkSMSourceProxy);

//----------------------------------------------------------------------------
vtkPVSource::vtkPVSource()
{
  this->CommandFunction = vtkPVSourceCommand;

  this->Parts = vtkCollection::New();

  this->DataInformationValid = 0;

  this->NumberOfOutputsInformation = vtkPVNumberOfOutputsInformation::New();
  
  // Number of instances cloned from this prototype
  this->PrototypeInstanceCount = 0;

  this->Name = 0;
  this->Label = 0;
  this->ModuleName = 0;
  this->MenuName = 0;
  this->ShortHelp = 0;
  this->LongHelp  = 0;

  // Initialize the data only after  Accept is invoked for the first time.
  // This variable is used to determine that.
  this->Initialized = 0;
  this->AcceptButtonRed = 0;

  // The notebook which holds Parameters, Display and Information pages.
  this->Notebook = vtkKWNotebook::New();
  this->Notebook->AlwaysShowTabsOn();

  this->PVInputs = NULL;
  this->NumberOfPVInputs = 0;
  this->PVOutput = NULL;

  this->NumberOfPVConsumers = 0;
  this->PVConsumers = 0;


  // The frame which contains the parameters related to the data source
  // and the Accept/Reset/Delete buttons.
  this->Parameters = vtkKWWidget::New();
  
  this->ParameterFrame = vtkKWFrame::New();
  this->ButtonFrame = vtkKWWidget::New();
  this->MainParameterFrame = vtkKWWidget::New();
  this->AcceptButton = vtkKWPushButton::New();
  this->ResetButton = vtkKWPushButton::New();
  this->DeleteButton = vtkKWPushButton::New();

  this->DescriptionFrame = vtkKWWidget::New();
  this->NameLabel = vtkKWLabeledLabel::New();
  this->TypeLabel = vtkKWLabeledLabel::New();
  this->LongHelpLabel = vtkKWLabeledLabel::New();
  this->LabelEntry = vtkKWLabeledEntry::New();
      
  this->Widgets = vtkPVWidgetCollection::New();
    
  this->ReplaceInput = 1;

  this->ParametersParent = NULL;
  this->View = NULL;

  this->VisitedFlag = 0;

  this->SourceClassName = 0;

  this->RequiredNumberOfInputParts = -1;
  this->VTKMultipleInputsFlag = 0;
  this->InputProperties = vtkCollection::New();

  this->VTKMultipleProcessFlag = 2;
  
  this->IsPermanent = 0;

  this->HideDisplayPage = 0;
  this->HideParametersPage = 0;
  this->HideInformationPage = 0;
  
  this->AcceptCallbackFlag = 0;

  this->SourceGrabbed = 0;

  this->ToolbarModule = 0;

  this->UpdateSourceInBatch = 0;

  this->LabelSetByUser = 0;

  this->Proxy = 0;
}

//----------------------------------------------------------------------------
vtkPVSource::~vtkPVSource()
{
  this->SetPVOutput(NULL);  
  this->RemoveAllPVInputs();

  this->Parts->Delete();
  this->Parts = NULL;

  this->NumberOfOutputsInformation->Delete();
  this->NumberOfOutputsInformation = NULL;
  
  if (this->PVConsumers)
    {
    delete [] this->PVConsumers;
    this->PVConsumers = NULL;
    this->NumberOfPVConsumers = 0;
    }

  vtkSMProxyManager* proxm = vtkSMObject::GetProxyManager();
  if (proxm && this->GetName())
    {
    proxm->UnRegisterProxy(this->GetName());
    }
  this->SetProxy(0);

  // Do not use SetName() or SetLabel() here. These make
  // the navigation window update when it should not.
  delete[] this->Name;
  delete[] this->Label;

  this->SetMenuName(0);
  this->SetShortHelp(0);
  this->SetLongHelp(0);

  // This is necessary in order to make the parent frame release it's
  // reference to the widgets. Otherwise, the widgets get deleted only
  // when the parent (usually the parameters notebook page) is deleted.
  this->Notebook->SetParent(0);
  this->Notebook->Delete();
  this->Notebook = NULL;

  this->Widgets->Delete();
  this->Widgets = NULL;

  this->AcceptButton->Delete();
  this->AcceptButton = NULL;  
  
  this->ResetButton->Delete();
  this->ResetButton = NULL;  
  
  this->DeleteButton->Delete();
  this->DeleteButton = NULL;

  this->DescriptionFrame->Delete();
  this->DescriptionFrame = NULL;

  this->NameLabel->Delete();
  this->NameLabel = NULL;

  this->TypeLabel->Delete();
  this->TypeLabel = NULL;

  this->LongHelpLabel->Delete();
  this->LongHelpLabel = NULL;

  this->LabelEntry->Delete();
  this->LabelEntry = NULL;

  this->ParameterFrame->Delete();
  this->ParameterFrame = NULL;

  this->MainParameterFrame->Delete();
  this->MainParameterFrame = NULL;

  this->ButtonFrame->Delete();
  this->ButtonFrame = NULL;

  this->Parameters->Delete();
  this->Parameters = NULL;
    
  if (this->ParametersParent)
    {
    this->ParametersParent->UnRegister(this);
    this->ParametersParent = NULL;
    }
  this->SetView(NULL);

  this->SetSourceClassName(0);
 
  this->InputProperties->Delete();
  this->InputProperties = NULL;

  this->SetModuleName(0);

}

//----------------------------------------------------------------------------
void vtkPVSource::AddPVInput(vtkPVSource *pvs)
{
  this->SetPVInputInternal("Input", this->NumberOfPVInputs, pvs, 0);
}

//----------------------------------------------------------------------------
void vtkPVSource::SetPVInput(const char* name, int idx, vtkPVSource *pvs)
{
  this->SetPVInputInternal(name, idx, pvs, 1);
}

//----------------------------------------------------------------------------
void vtkPVSource::SetPVInputInternal(
  const char* iname, int idx, vtkPVSource *pvs, int doInit)
{
  vtkPVApplication *pvApp = this->GetPVApplication();

  if (pvApp == NULL)
    {
    vtkErrorMacro(
      "No Application. Create the source before setting the input.");
    return;
    }
  // Handle visibility of old and new input.
  if (this->ReplaceInput)
    {
    vtkPVSource *oldInput = this->GetNthPVInput(idx);
    if (oldInput)
      {
      oldInput->SetVisibility(1);
      this->GetPVRenderView()->EventuallyRender();
      }
    }

  if (this->Proxy)
    {
    vtkSMProxyProperty* inputp = vtkSMProxyProperty::SafeDownCast(
      this->Proxy->GetProperty(iname));
    if (inputp)
      {
      if (doInit)
        {
        inputp->RemoveAllProxies();
        }
      inputp->AddProxy(pvs->GetProxy());
      }
    }

  // Set the paraview reference to the new input.
  this->SetNthPVInput(idx, pvs);
  if (pvs == NULL)
    {
    return;
    }

  this->GetPVRenderView()->UpdateNavigationWindow(this, 0);
}



//----------------------------------------------------------------------------
void vtkPVSource::AddPVConsumer(vtkPVSource *c)
{
  // make sure it isn't already there
  if (this->IsPVConsumer(c))
    {
    return;
    }
  // add it to the list, reallocate memory
  vtkPVSource **tmp = this->PVConsumers;
  this->NumberOfPVConsumers++;
  this->PVConsumers = new vtkPVSource* [this->NumberOfPVConsumers];
  for (int i = 0; i < (this->NumberOfPVConsumers-1); i++)
    {
    this->PVConsumers[i] = tmp[i];
    }
  this->PVConsumers[this->NumberOfPVConsumers-1] = c;
  // free old memory
  delete [] tmp;
}

//----------------------------------------------------------------------------
void vtkPVSource::RemovePVConsumer(vtkPVSource *c)
{
  // make sure it is already there
  if (!this->IsPVConsumer(c))
    {
    return;
    }
  // remove it from the list, reallocate memory
  vtkPVSource **tmp = this->PVConsumers;
  this->NumberOfPVConsumers--;
  this->PVConsumers = new vtkPVSource* [this->NumberOfPVConsumers];
  int cnt = 0;
  int i;
  for (i = 0; i <= this->NumberOfPVConsumers; i++)
    {
    if (tmp[i] != c)
      {
      this->PVConsumers[cnt] = tmp[i];
      cnt++;
      }
    }
  // free old memory
  delete [] tmp;
}

//----------------------------------------------------------------------------
int vtkPVSource::IsPVConsumer(vtkPVSource *c)
{
  int i;
  for (i = 0; i < this->NumberOfPVConsumers; i++)
    {
    if (this->PVConsumers[i] == c)
      {
      return 1;
      }
    }
  return 0;
}

//----------------------------------------------------------------------------
vtkPVSource *vtkPVSource::GetPVConsumer(int i)
{
  if (i >= this->NumberOfPVConsumers)
    {
    return 0;
    }
  return this->PVConsumers[i];
}





//----------------------------------------------------------------------------
int vtkPVSource::GetNumberOfParts()
{
  return this->Parts->GetNumberOfItems();
}

//----------------------------------------------------------------------------
void vtkPVSource::SetPart(vtkSMPart* part)
{
  if ( !part )
    {
    return;
    }
  this->Parts->AddItem(part);
}

//----------------------------------------------------------------------------
void vtkPVSource::AddPart(vtkSMPart* part)
{
  if ( !part )
    {
    return;
    }
  this->Parts->AddItem(part);
}

//----------------------------------------------------------------------------
vtkSMPart* vtkPVSource::GetPart(int idx)
{
  vtkSMPart *part;

  part = static_cast<vtkSMPart*>(this->Parts->GetItemAsObject(idx));
  return part;
}

//----------------------------------------------------------------------------
vtkPVDataInformation* vtkPVSource::GetDataInformation()
{
  if (this->DataInformationValid == 0)
    {
    this->GatherDataInformation();
    }
  return this->Proxy->GetDataInformation();
}

//----------------------------------------------------------------------------
void vtkPVSource::InvalidateDataInformation()
{
  this->DataInformationValid = 0;
}

//----------------------------------------------------------------------------
void vtkPVSource::GatherDataInformation()
{
  vtkSMPart *part;

  this->Parts->InitTraversal();
  while ( ( part = (vtkSMPart*)(this->Parts->GetNextItemAsObject())) )
    {
    part->GatherDataInformation();
    }
  this->DataInformationValid = 1;

  if (this->GetPVOutput())
    {
    // This will cause a recursive call, but the Valid flag will terminate it.
    this->GetPVOutput()->UpdateProperties();
    }
}

//----------------------------------------------------------------------------
void vtkPVSource::Update()
{
  vtkSMPart *part;

  this->Parts->InitTraversal();
  while ( (part = (vtkSMPart*)(this->Parts->GetNextItemAsObject())) )
    {
    part->Update();
    }
}

//----------------------------------------------------------------------------
void vtkPVSource::UpdateEnableState()
{
  this->Superclass::UpdateEnableState();

  this->PropagateEnableState(this->PVOutput);
  this->PropagateEnableState(this->Notebook);
  this->PropagateEnableState(this->Parameters);
  this->PropagateEnableState(this->MainParameterFrame);
  this->PropagateEnableState(this->ButtonFrame);
  this->PropagateEnableState(this->ParameterFrame);

  if ( this->Widgets )
    {
    vtkPVWidget *pvWidget;
    vtkCollectionIterator *it = this->Widgets->NewIterator();
    it->InitTraversal();

    int i;
    for (i = 0; i < this->Widgets->GetNumberOfItems(); i++)
      {
      pvWidget = static_cast<vtkPVWidget*>(it->GetObject());
      pvWidget->SetEnabled(this->Enabled);
      it->GoToNextItem();
      }
    it->Delete();
    }

  this->PropagateEnableState(this->AcceptButton);
  this->PropagateEnableState(this->ResetButton);
  if ( this->IsDeletable() )
    {
    this->PropagateEnableState(this->DeleteButton);
    }
  else
    {
    this->DeleteButton->SetEnabled(0);
    }
  this->PropagateEnableState(this->DescriptionFrame);
  this->PropagateEnableState(this->NameLabel);
  this->PropagateEnableState(this->TypeLabel);
  this->PropagateEnableState(this->LabelEntry);
  this->PropagateEnableState(this->LongHelpLabel);
}
  
//----------------------------------------------------------------------------
void vtkPVSource::SetVisibilityInternal(int v)
{
  vtkSMPart *part;
  int idx, num;

  if (this->GetPVOutput())
    {
    this->GetPVOutput()->SetVisibilityCheckState(v);
    }

  num = this->GetNumberOfParts();
  for (idx = 0; idx < num; ++idx)
    {
    part = this->GetPart(idx);
    if (part)
      {
      part->GetPartDisplay()->SetVisibility(v);
      }
    }
}


//----------------------------------------------------------------------------
// Functions to update the progress bar
void vtkPVSourceStartProgress(void* vtkNotUsed(arg))
{
  //vtkPVSource *me = (vtkPVSource*)arg;
  //vtkSource *vtkSource = me->GetVTKSource();
  //static char str[200];
  
  //if (vtkSource && me->GetWindow())
  //  {
  //  sprintf(str, "Processing %s", vtkSource->GetClassName());
  //  me->GetWindow()->SetStatusText(str);
  //  }
}
//----------------------------------------------------------------------------
void vtkPVSourceReportProgress(void* vtkNotUsed(arg))
{
  //vtkPVSource *me = (vtkPVSource*)arg;
  //vtkSource *vtkSource = me->GetVTKSource();

  //if (me->GetWindow())
  //  {
  //  me->GetWindow()->GetProgressGauge()->SetValue((int)(vtkSource->GetProgress() * 100));
  //  }
}
//----------------------------------------------------------------------------
void vtkPVSourceEndProgress(void* vtkNotUsed(arg))
{
  //vtkPVSource *me = (vtkPVSource*)arg;
  
  //if (me->GetWindow())
  //  {
  //  me->GetWindow()->SetStatusText("");
  //  me->GetWindow()->GetProgressGauge()->SetValue(0);
  //  }
}


int vtkPVSource::GetNumberOfVTKSources()
{
  if (!this->Proxy)
    {
    return 0;
    }
  return this->Proxy->GetNumberOfIDs();
}

//----------------------------------------------------------------------------
unsigned int vtkPVSource::GetVTKSourceIDAsInt(int idx)
{
  vtkClientServerID id = this->GetVTKSourceID(idx);
  return id.ID;
}

//----------------------------------------------------------------------------
vtkClientServerID vtkPVSource::GetVTKSourceID(int idx)
{
  if(idx >= this->GetNumberOfVTKSources() || !this->Proxy)
    {
    vtkClientServerID id = {0};
    return id;
    }
  return this->Proxy->GetID(idx);
}

//----------------------------------------------------------------------------
vtkPVWindow* vtkPVSource::GetPVWindow()
{
  vtkPVApplication *pvApp = this->GetPVApplication();

  if (pvApp == NULL)
    {
    return NULL;
    }
  
  return pvApp->GetMainWindow();
}

//----------------------------------------------------------------------------
vtkPVApplication* vtkPVSource::GetPVApplication()
{
  if (this->GetApplication() == NULL)
    {
    return NULL;
    }
  
  if (this->GetApplication()->IsA("vtkPVApplication"))
    {  
    return (vtkPVApplication*)(this->GetApplication());
    }
  else
    {
    vtkErrorMacro("Bad typecast");
    return NULL;
    } 
}

//----------------------------------------------------------------------------
void vtkPVSource::CreateProperties()
{
  // If the user has not set the parameters parent.
  if (this->ParametersParent == NULL)
    {
    vtkErrorMacro("ParametersParent has not been set.");
    }

  this->Notebook->SetParent(this->ParametersParent);
  this->Notebook->Create(this->GetApplication(),"");

  // Set up the pages of the notebook.
  if (!this->HideParametersPage)
    {
    this->Notebook->AddPage("Parameters");
    this->Parameters->SetParent(this->Notebook->GetFrame("Parameters"));
    }
  else
    {
    this->Parameters->SetParent(this->ParametersParent);
    }

  this->Parameters->Create(this->GetApplication(),"frame","");
  if (!this->HideParametersPage)
    {
    this->Script("pack %s -pady 2 -fill x -expand yes",
                 this->Parameters->GetWidgetName());
    }

  // For initializing the trace of the notebook.
  this->GetParametersParent()->SetTraceReferenceObject(this);
  this->GetParametersParent()->SetTraceReferenceCommand("GetParametersParent");

  // Set the description frame
  // Try to do something that looks like the parameters, i.e. fixed-width
  // labels and "expandable" values. This has to be fixed later when the
  // parameters will be properly aligned (i.e. gridded)

  this->DescriptionFrame ->SetParent(this->Parameters);
  this->DescriptionFrame->Create(this->GetApplication(), "frame", "");
  this->Script("pack %s -fill both -expand t -side top -padx 2 -pady 2", 
               this->DescriptionFrame->GetWidgetName());

  const char *label1_opt = "-width 12 -anchor e";

  this->NameLabel->SetParent(this->DescriptionFrame);
  this->NameLabel->Create(this->GetApplication());
  this->NameLabel->SetLabel("Name:");
  this->Script("%s configure -anchor w", 
               this->NameLabel->GetLabel2()->GetWidgetName());
  this->Script("%s config %s", 
               this->NameLabel->GetLabel()->GetWidgetName(), label1_opt);
  this->Script("pack %s -fill x -expand t", 
               this->NameLabel->GetLabel2()->GetWidgetName());
  vtkKWTkUtilities::ChangeFontWeightToBold(
    this->GetApplication()->GetMainInterp(),
    this->NameLabel->GetLabel2()->GetWidgetName());

  this->TypeLabel->SetParent(this->DescriptionFrame);
  this->TypeLabel->Create(this->GetApplication());
  this->TypeLabel->GetLabel()->SetLabel("Class:");
  this->Script("%s configure -anchor w", 
               this->TypeLabel->GetLabel2()->GetWidgetName());
  this->Script("%s config %s", 
               this->TypeLabel->GetLabel()->GetWidgetName(), label1_opt);
  this->Script("pack %s -fill x -expand t", 
               this->TypeLabel->GetLabel2()->GetWidgetName());

  this->LabelEntry->SetParent(this->DescriptionFrame);
  this->LabelEntry->Create(this->GetApplication());
  this->LabelEntry->GetLabel()->SetLabel("Label:");
  this->Script("%s config %s", 
               this->LabelEntry->GetLabel()->GetWidgetName(),label1_opt);
  this->Script("pack %s -fill x -expand t", 
               this->LabelEntry->GetEntry()->GetWidgetName());
  this->Script("bind %s <KeyPress-Return> {%s LabelEntryCallback}",
               this->LabelEntry->GetEntry()->GetWidgetName(), 
               this->GetTclName());

  this->LongHelpLabel->SetParent(this->DescriptionFrame);
  this->LongHelpLabel->Create(this->GetApplication());
  this->LongHelpLabel->GetLabel()->SetLabel("Description:");
  this->LongHelpLabel->GetLabel2()->AdjustWrapLengthToWidthOn();
  this->Script("%s configure -anchor w", 
               this->LongHelpLabel->GetLabel2()->GetWidgetName());
  this->Script("%s config %s", 
               this->LongHelpLabel->GetLabel()->GetWidgetName(), label1_opt);
  this->Script("pack %s -fill x -expand t", 
               this->LongHelpLabel->GetLabel2()->GetWidgetName());

  this->Script("grid %s -sticky news", 
               this->NameLabel->GetWidgetName());
  this->Script("grid %s -sticky news", 
               this->TypeLabel->GetWidgetName());
  this->Script("grid %s -sticky news", 
               this->LabelEntry->GetWidgetName());
  this->Script("grid %s -sticky news", 
               this->LongHelpLabel->GetWidgetName());
  this->Script("grid columnconfigure %s 0 -weight 1", 
               this->LongHelpLabel->GetParent()->GetWidgetName());

  // The main parameter frame

  this->MainParameterFrame->SetParent(this->Parameters);
  this->MainParameterFrame->Create(this->GetApplication(), "frame", "");
  this->Script("pack %s -fill both -expand t -side top", 
               this->MainParameterFrame->GetWidgetName());

  this->ButtonFrame->SetParent(this->MainParameterFrame);
  this->ButtonFrame->Create(this->GetApplication(), "frame", "");
  this->Script("pack %s -fill both -expand t -side top", 
               this->ButtonFrame->GetWidgetName());

  this->ParameterFrame->SetParent(this->MainParameterFrame);

  this->ParameterFrame->ScrollableOn();
  this->ParameterFrame->Create(this->GetApplication(),0);
  this->Script("pack %s -fill both -expand t -side top", 
               this->ParameterFrame->GetWidgetName());

  vtkKWWidget *frame = vtkKWWidget::New();
  frame->SetParent(this->ButtonFrame);
  frame->Create(this->GetApplication(), "frame", "");
  this->Script("pack %s -fill x -expand t", frame->GetWidgetName());  
  
  this->AcceptButton->SetParent(frame);
  this->AcceptButton->Create(this->GetApplication(), 
                             "-text Accept");
  this->AcceptButton->SetCommand(this, "PreAcceptCallback");
  this->AcceptButton->SetBalloonHelpString(
    "Cause the current values in the user interface to take effect");

  this->ResetButton->SetParent(frame);
  this->ResetButton->Create(this->GetApplication(), "-text Reset");
  this->ResetButton->SetCommand(this, "ResetCallback");
  this->ResetButton->SetBalloonHelpString(
    "Revert to the previous parameters of the module.");

  this->DeleteButton->SetParent(frame);
  this->DeleteButton->Create(this->GetApplication(), "-text Delete");
  this->DeleteButton->SetCommand(this, "DeleteCallback");
  this->DeleteButton->SetBalloonHelpString(
    "Remove the current module.  "
    "This can only be done if no other modules depends on the current one.");

  this->Script("pack %s %s %s -padx 2 -pady 2 -side left -fill x -expand t",
               this->AcceptButton->GetWidgetName(), 
               this->ResetButton->GetWidgetName(), 
               this->DeleteButton->GetWidgetName());
  this->Script("bind %s <Enter> {+focus %s}",
               this->AcceptButton->GetWidgetName(),
               this->AcceptButton->GetWidgetName());

  frame->Delete();  
 
  this->UpdateProperties();

  vtkPVWidget *pvWidget;
  vtkCollectionIterator *it = this->Widgets->NewIterator();
  it->InitTraversal();
  
  int i;
  for (i = 0; i < this->Widgets->GetNumberOfItems(); i++)
    {
    pvWidget = static_cast<vtkPVWidget*>(it->GetObject());
    pvWidget->SetParent(this->ParameterFrame->GetFrame());
    pvWidget->Create(this->GetApplication());
    this->Script("pack %s -side top -fill x -expand t", 
                 pvWidget->GetWidgetName());
    it->GoToNextItem();
    }
  it->Delete();
}

//----------------------------------------------------------------------------
void vtkPVSource::UpdateDescriptionFrame()
{
  if (!this->GetApplication())
    {
    return;
    }

  if (this->NameLabel && this->NameLabel->IsCreated())
    {
    this->NameLabel->GetLabel2()->SetLabel(this->Name ? this->Name : "");
    }

  if (this->TypeLabel && this->TypeLabel->IsCreated())
    {
    if (this->GetSourceClassName()) 
      {
      this->TypeLabel->GetLabel2()->SetLabel(
        this->GetSourceClassName());
//        this->GetVTKSource()->GetClassName());
      if (this->DescriptionFrame->IsPacked())
        {
        this->Script("grid %s", this->TypeLabel->GetWidgetName());
        }
      }
    else
      {
      this->TypeLabel->GetLabel2()->SetLabel("");
      if (this->DescriptionFrame->IsPacked())
        {
        this->Script("grid remove %s", this->TypeLabel->GetWidgetName());
        }
      }
    }

  if (this->LabelEntry && this->LabelEntry->IsCreated())
    {
    this->LabelEntry->GetEntry()->SetValue(this->Label);
    }

  if (this->LongHelpLabel && this->LongHelpLabel->IsCreated())
    {
    if (this->LongHelp && 
        !(this->GetPVApplication() && 
          !this->GetPVApplication()->GetShowSourcesLongHelp())) 
      {
      this->LongHelpLabel->GetLabel2()->SetLabel(this->LongHelp);
      if (this->DescriptionFrame->IsPacked())
        {
        this->Script("grid %s", this->LongHelpLabel->GetWidgetName());
        }
      }
    else
      {
      this->LongHelpLabel->GetLabel2()->SetLabel("");
      if (this->DescriptionFrame->IsPacked())
        {
        this->Script("grid remove %s", this->LongHelpLabel->GetWidgetName());
        }
      }
    }
}

//----------------------------------------------------------------------------
void vtkPVSource::GrabFocus()
{
  this->SourceGrabbed = 1;

  this->GetPVRenderView()->UpdateNavigationWindow(this, 1);
}

//----------------------------------------------------------------------------
void vtkPVSource::UnGrabFocus()
{

  if ( this->SourceGrabbed )
    {
    this->GetPVRenderView()->UpdateNavigationWindow(this, 0);
    }
  this->SourceGrabbed = 0;
  this->GetPVWindow()->UpdateEnableState();
}

//----------------------------------------------------------------------------
void vtkPVSource::Pack()
{
  // The update is needed to work around a packing problem which
  // occur for large windows. Do not remove.
  this->GetPVRenderView()->UpdateTclButAvoidRendering();

  this->Script("catch {eval pack forget [pack slaves %s]}",
               this->ParametersParent->GetWidgetName());
  this->Script("pack %s -side top -fill both -expand t",
               this->GetPVRenderView()->GetNavigationFrame()->GetWidgetName());
  this->Script("pack %s -pady 2 -padx 2 -fill both -expand yes -anchor n",
               this->Notebook->GetWidgetName());
}

//----------------------------------------------------------------------------
void vtkPVSource::Select()
{
  this->Pack();

  vtkPVData *data;
  
  this->UpdateProperties();
  // This may best be merged with the UpdateProperties call but ...
  // We make the call here to update the input menu, 
  // which is now just another pvWidget.
  this->UpdateParameterWidgets();
  
  data = this->GetPVOutput();
  if (data)
    {
    // This has a side effect of gathering and displaying information.
    data->GetPVSource()->GetDataInformation();
    }

  if (this->GetPVRenderView())
    {
    this->GetPVRenderView()->UpdateNavigationWindow(this, this->SourceGrabbed);
    }

  int i;
  vtkPVWidget *pvWidget = 0;
  vtkCollectionIterator *it = this->Widgets->NewIterator();
  it->InitTraversal();
  for (i = 0; i < this->Widgets->GetNumberOfItems(); i++)
    {
    pvWidget = static_cast<vtkPVWidget*>(it->GetObject());
    pvWidget->Select();
    it->GoToNextItem();
    }
  it->Delete();
}

//----------------------------------------------------------------------------
void vtkPVSource::Deselect(int doPackForget)
{
  if (doPackForget)
    {
    this->Script("pack forget %s", this->Notebook->GetWidgetName());
    }
  int i;
  vtkPVWidget *pvWidget = 0;
  vtkCollectionIterator *it = this->Widgets->NewIterator();
  it->InitTraversal();
  
  for (i = 0; i < this->Widgets->GetNumberOfItems(); i++)
    {
    pvWidget = static_cast<vtkPVWidget*>(it->GetObject());
    pvWidget->Deselect();
    it->GoToNextItem();
    }
  it->Delete();
}


//----------------------------------------------------------------------------
char* vtkPVSource::GetName()
{
  return this->Name;
}

//----------------------------------------------------------------------------
void vtkPVSource::SetName (const char* arg) 
{ 
  vtkDebugMacro(<< this->GetClassName() << " (" << this << "): setting " 
                << this->Name << " to " << arg ); 
  if ( this->Name && arg && (!strcmp(this->Name,arg))) 
    { 
    return;
    } 
  if (this->Name) 
    { 
    delete [] this->Name; 
    } 
  if (arg) 
    { 
    this->Name = new char[strlen(arg)+1]; 
    strcpy(this->Name,arg); 
    } 
  else 
    { 
    this->Name = NULL;
    }
  this->Modified();
  
  // Make sure the description frame is upto date.
  this->UpdateDescriptionFrame();

  // Update the nav window (that usually might display name + description)
  if (this->GetPVRenderView())
    {
    this->GetPVRenderView()->UpdateNavigationWindow(this, this->SourceGrabbed);
    }
} 

//----------------------------------------------------------------------------
char* vtkPVSource::GetLabel() 
{ 
  // Design choice: if the description is empty, initialize it with the
  // Tcl name, so that the user knows what to overrides in the nav window.

  if (this->Label == NULL)
    {
    this->SetLabelNoTrace(this->GetName());
    }
  return this->Label;
}

//----------------------------------------------------------------------------
void vtkPVSource::SetLabelOnce(const char* arg) 
{
  if (!this->LabelSetByUser)
    {
    this->SetLabelNoTrace(arg);
    }
}

//----------------------------------------------------------------------------
void vtkPVSource::SetLabel(const char* arg) 
{ 
  this->LabelSetByUser = 1;

  this->SetLabelNoTrace(arg);

  if ( !this->GetApplication() )
    {
    return;
    }
  // Update the nav window (that usually might display name + description)
  vtkPVSource* current = this->GetPVWindow()->GetCurrentPVSource();
  if (this->GetPVRenderView() && current)
    {
    this->GetPVRenderView()->UpdateNavigationWindow(
      current, current->SourceGrabbed);
    }
  // Trace here, not in SetLabel (design choice)
  this->GetPVApplication()->AddTraceEntry("$kw(%s) SetLabel {%s}",
                                          this->GetTclName(),
                                          this->Label);
  this->GetPVApplication()->AddTraceEntry("$kw(%s) LabelEntryCallback",
                                          this->GetTclName());
}

//----------------------------------------------------------------------------
void vtkPVSource::SetLabelNoTrace(const char* arg) 
{ 
  vtkDebugMacro(<< this->GetClassName() << " (" << this << "): setting " 
                << this->Label << " to " << arg ); 
  if ( this->Label && arg && (!strcmp(this->Label,arg))) 
    { 
    return;
    } 
  if (this->Label) 
    { 
    delete [] this->Label; 
    } 
  if (arg) 
    { 
    this->Label = new char[strlen(arg)+1]; 
    strcpy(this->Label,arg); 
    } 
  else 
    { 
    this->Label = NULL;
    }
  this->Modified();

  // Make sure the description frame is upto date.
  this->UpdateDescriptionFrame();

  vtkPVWindow *window = this->GetPVWindow();
  if (window)
    {
    window->UpdateSelectMenu();
    }

} 

//----------------------------------------------------------------------------
void vtkPVSource::LabelEntryCallback()
{
  this->SetLabel(this->LabelEntry->GetEntry()->GetValue());
}

//----------------------------------------------------------------------------
void vtkPVSource::SetVisibility(int v)
{
  if (this->PVOutput)
    {
    this->PVOutput->SetVisibility(v);
    }
}

//----------------------------------------------------------------------------
int vtkPVSource::GetVisibility()
{
  if ( this->GetPVOutput() && this->GetPVOutput()->GetVisibility() )
    {
    return 1;
    }
  return 0;
}

//----------------------------------------------------------------------------
void vtkPVSource::AcceptCallback()
{
  // This method is purposely not virtual.  The AcceptCallbackFlag
  // must be 1 for the duration of the accept callback no matter what
  // subclasses might do.  All of the real AcceptCallback funcionality
  // should be implemented in AcceptCallbackInternal.
  this->AcceptCallbackFlag = 1;
  this->AcceptCallbackInternal();
  this->AcceptCallbackFlag = 0;
}

//----------------------------------------------------------------------------
void vtkPVSource::PreAcceptCallback()
{
  if ( ! this->AcceptButtonRed)
    {
    return;
    }
  this->Script("%s configure -cursor watch",
               this->GetPVWindow()->GetWidgetName());
  this->Script("after idle {%s AcceptCallback}", this->GetTclName());
}

//----------------------------------------------------------------------------
void vtkPVSource::AcceptCallbackInternal()
{
  // I had trouble with this object destructing too early
  // in this method, because of an unregistered reference ...
  // It was an error condition (no output was created).
  this->Register(this);

  int dialogAlreadyUp=0;
  // This is here to prevent the user from killing the
  // application from inside the accept callback.
  if (!this->GetApplication()->GetDialogUp())
    {
    this->GetApplication()->SetDialogUp(1);
    }
  else
    {
    dialogAlreadyUp=1;
    }
  this->Accept(0);
  this->GetPVApplication()->AddTraceEntry("$kw(%s) AcceptCallback",
                                          this->GetTclName());
  if (!dialogAlreadyUp)
    {
    this->GetApplication()->SetDialogUp(0);
    }


  // I had trouble with this object destructing too early
  // in this method, because of an unregistered reference ...
  this->UnRegister(this);
}

//----------------------------------------------------------------------------
void vtkPVSource::Accept(int hideFlag, int hideSource)
{
  vtkPVWindow *window;
  vtkPVSource *input;

  // vtkPVSource is taking over some of the update descisions because
  // client does not have a real pipeline.
  if ( ! this->AcceptButtonRed)
    {
    return;
    } 

  this->GetPVApplication()->GetProcessModule()->SendPrepareProgress();

  window = this->GetPVWindow();

  this->SetAcceptButtonColorToUnmodified();
  this->GetPVRenderView()->UpdateTclButAvoidRendering();
  
  // We need to pass the parameters from the UI to the VTK objects before
  // we check whether to insert ExtractPieces.  Otherwise, we'll get errors
  // about unspecified file names, etc., when ExecuteInformation is called on
  // the VTK source.  (The vtkPLOT3DReader is a good example of this.)
  this->UpdateVTKSourceParameters();

  this->MarkSourcesForUpdate();

  // Moved from creation of the source. (InitializeClone)
  // Initialize the output if necessary.
  // This has to be after the widgets are accepted (UpdateVTKSOurceParameters)
  // because they can change the number of parts.
  if (this->GetPVOutput() == NULL)
    { // This is the first time, create the data.
    this->InitializeData();
    }
    
  // Initialize the output if necessary.
  if ( ! this->Initialized)
    { // This is the first time, initialize data.    
    vtkPVData *pvd;
    
    pvd = this->GetPVOutput();
    if (pvd == NULL)
      { // I suppose we should try and delete the source.
      vtkErrorMacro("Could not get output.");
      this->DeleteCallback();    
      this->GetPVApplication()->GetProcessModule()->SendCleanupPendingProgress();
      return;
      }

    if (!this->GetHideDisplayPage())
      {
      this->Notebook->AddPage("Display");
      }
    if (!this->GetHideInformationPage())
      {
      this->Notebook->AddPage("Information");
      }

    pvd->CreateProperties();
    this->GetPVApplication()->GetProcessModule()->GetRenderModule()->AddSource(this->GetProxy());

    // Make the last data invisible.
    input = this->GetPVInput(0);
    if (input)
      {
      if (this->ReplaceInput && 
          input->GetPVOutput()->GetPropertiesCreated() && 
          hideSource)
        {
        input->SetVisibilityInternal(0);
        }
      }

    // Set the current data of the window.
    if ( ! hideFlag)
      {
      window->SetCurrentPVSource(this);
      }
    else
      {
      this->SetVisibilityInternal(0);
      }

    // We need to update so we will have correct information for initialization.
    if (this->GetPVOutput())
      {
      // Update the VTK data.
      this->Update();
      }

    // The best test I could come up with to only reset
    // the camera when the first source is created.
    if (window->GetSourceList("Sources")->GetNumberOfItems() == 1)
      {
      double bds[6];
      this->GetDataInformation()->GetBounds(bds);
      if (bds[0] <= bds[1] && bds[2] <= bds[3] && bds[4] <= bds[5])
        {
        window->SetCenterOfRotation(0.5*(bds[0]+bds[1]), 
                                    0.5*(bds[2]+bds[3]),
                                    0.5*(bds[4]+bds[5]));
        window->ResetCenterCallback();
        window->GetMainView()->GetRenderer()->ResetCamera(
          bds[0], bds[1], bds[2], bds[3], bds[4], bds[5]);
        }
      }

    pvd->Initialize();
    // This causes input to be checked for validity.
    // I put it at the end so the InputFixedTypeRequirement will work.
    this->UnGrabFocus();
    this->Initialized = 1;
    }
  else
    {
    this->GetPVWindow()->UpdateEnableState();
    }

  window->GetMenuView()->CheckRadioButton(
                                  window->GetMenuView(), "Radio", 2);
  this->UpdateProperties();
  this->GetPVRenderView()->EventuallyRender();

  // Update the selection menu.
  window->UpdateSelectMenu();

  // Regenerate the data property page in case something has changed.
  vtkPVData *pvd = this->GetPVOutput();
  if (pvd)
    {
    this->Update();
    pvd->UpdateProperties();
    }

  this->GetPVRenderView()->UpdateTclButAvoidRendering();

#ifdef _WIN32
  this->Script("%s configure -cursor arrow", window->GetWidgetName());
#else
  this->Script("%s configure -cursor left_ptr", window->GetWidgetName());
#endif  

  // Causes the data information to be updated if the filter executed.
  // Note has to be done here because tcl update causes render which
  // causes the filter to execute.
  pvd->UpdateProperties();
  
  this->GetPVApplication()->GetProcessModule()->SendCleanupPendingProgress();
}

//----------------------------------------------------------------------------
void vtkPVSource::MarkSourcesForUpdate()
{
  int idx;
  vtkPVSource* consumer;

  this->InvalidateDataInformation();
  this->Proxy->MarkConsumersAsModified();

  // Get rid of caches.
  int numParts;
  vtkSMPart *part;
  numParts = this->GetNumberOfParts();
  for (idx = 0; idx < numParts; ++idx)
    {
    part = this->GetPart(idx);
    part->MarkForUpdate();
    }

  for (idx = 0; idx < this->NumberOfPVConsumers; ++idx)
    {
    consumer = this->GetPVConsumer(idx);
    consumer->MarkSourcesForUpdate();
    }  

}

//----------------------------------------------------------------------------
void vtkPVSource::ResetCallback()
{
  this->UpdateParameterWidgets();
  if (this->Initialized)
    {
    this->GetPVRenderView()->EventuallyRender();
    this->Script("update");

    this->SetAcceptButtonColorToUnmodified();
    }
}

//---------------------------------------------------------------------------
void vtkPVSource::DeleteCallback()
{
  int i;
  int initialized = this->Initialized;
  vtkPVSource *prev = NULL;
  vtkPVSource *current = 0;
  vtkPVWindow *window = this->GetPVWindow();

  window->GetAnimationInterface()->DeleteSource(this);

  if (this->GetPVOutput())
    {  
    this->GetPVOutput()->DeleteCallback();
    }

  // Just in case cursor was left in a funny state.
#ifdef _WIN32
  this->Script("%s configure -cursor arrow", window->GetWidgetName());
#else
  this->Script("%s configure -cursor left_ptr", window->GetWidgetName());
#endif  

  if ( ! this->Initialized)
    {
    // Remove the local grab
    this->UnGrabFocus();

    this->Script("update");   
    this->Initialized = 1;
    }
  else
    {
    this->GetPVWindow()->UpdateEnableState();
    }
  
  if (this->GetNumberOfPVConsumers() > 0 )
    {
    vtkErrorMacro("An output is used.  We cannot delete this source.");
    return;
    }
  
  // Save this action in the trace file.
  this->GetPVApplication()->AddTraceEntry("$kw(%s) DeleteCallback",
                                          this->GetTclName());

  // Get the input so we can make it visible and make it current.
  if (this->GetNumberOfPVInputs() > 0)
    {
    prev = this->PVInputs[0];
    // Just a sanity check
    if (prev == NULL)
      {
      vtkErrorMacro("Expecting an input but none found.");
      }
    else
      {
      prev->SetVisibilityInternal(1);
      }
    }

  // Remove this source from the inputs users collection.
  for (i = 0; i < this->GetNumberOfPVInputs(); i++)
    {
    if (this->PVInputs[i])
      {
      this->PVInputs[i]->RemovePVConsumer(this);
      }
    }
    
  // Look for a source to make current.
  if (prev == NULL)
    {
    prev = this->GetPVWindow()->GetPreviousPVSource();
    }
  // Just remember it. We set the current pv source later.
  //this->GetPVWindow()->SetCurrentPVSourceCallback(prev);
  if ( prev == NULL && window->GetSourceList("Sources")->GetNumberOfItems() > 0 )
    {
    vtkCollectionIterator *it = window->GetSourceList("Sources")->NewIterator();
    it->InitTraversal();
    while ( !it->IsDoneWithTraversal() )
      {
      prev = static_cast<vtkPVSource*>( it->GetObject() );
      if ( prev != this )
        {
        break;
        }
      else
        {
        prev = 0;
        }
      it->GoToNextItem();
      }
    it->Delete();
    }

  current = window->GetCurrentPVSource();
  if ( this == current || window->GetSourceList("Sources")->GetNumberOfItems() == 1)
    {
    current = prev;
  
    if (prev == NULL)
      {
      // Unpack the properties.  This is required if prev is NULL.
      this->Script("catch {eval pack forget [pack slaves %s]}",
                   this->ParametersParent->GetWidgetName());
      
      // Show the 3D View settings
      vtkPVApplication *pvApp = 
        vtkPVApplication::SafeDownCast(this->GetApplication());
      vtkPVWindow *iwindow = pvApp->GetMainWindow();
      this->Script("%s invoke \"%s\"", 
                   iwindow->GetMenuView()->GetWidgetName(),
                   VTK_PV_VIEW_MENU_LABEL);
      }
    }
        
  // Remove all of the actors mappers. from the renderer.
  if (this->PVOutput)
    {
    this->GetPVApplication()->GetProcessModule()->GetRenderModule()->RemoveSource(this->GetProxy());
    }    

  this->SetPVOutput(NULL);
  
  if ( initialized )
    {
    this->GetPVRenderView()->EventuallyRender();
    }
  
  // This should delete this source.
  // "this" will no longer be valid after the call.
  window->RemovePVSource("Sources", this);
  window->SetCurrentPVSourceCallback(current);
}

//----------------------------------------------------------------------------
void vtkPVSource::UpdateParameterWidgets()
{
  vtkPVWidget *pvw;
  vtkCollectionIterator *it = this->Widgets->NewIterator();
  it->InitTraversal();
  while( !it->IsDoneWithTraversal() )
    {
    pvw = static_cast<vtkPVWidget*>(it->GetObject());
    // Do not try to reset the widget if it is not initialized
    if (pvw->GetApplication())
      {
      pvw->Reset();
      }
    it->GoToNextItem();
    }
  it->Delete();
}

//----------------------------------------------------------------------------
void vtkPVSource::RaiseSourcePage()
{
  this->Notebook->Raise("Source");
}

//----------------------------------------------------------------------------
// This should be apart of AcceptCallbackInternal.
void vtkPVSource::UpdateVTKSourceParameters()
{
  vtkPVWidget *pvw;
  vtkCollectionIterator *it;

  it = this->Widgets->NewIterator();
  it->InitTraversal();
  while( !it->IsDoneWithTraversal() )
    {
    pvw = static_cast<vtkPVWidget*>(it->GetObject());
    if (pvw && pvw->GetModifiedFlag())
      {
      pvw->Accept();
      }
    it->GoToNextItem();
    }

  if (this->Proxy)
    {
    this->Proxy->UpdateVTKObjects();
    }

  it->InitTraversal();
  while( !it->IsDoneWithTraversal() )
    {
    pvw = static_cast<vtkPVWidget*>(it->GetObject());
    if (pvw)
      {
      pvw->PostAccept();
      }
    it->GoToNextItem();
    }

  it->Delete();
}

//----------------------------------------------------------------------------
void vtkPVSource::UpdateProperties()
{
  this->UpdateEnableState();
  this->UpdateDescriptionFrame();
}

//----------------------------------------------------------------------------
int vtkPVSource::IsDeletable()
{
  if (this->IsPermanent || this->GetNumberOfPVConsumers() > 0 )
    {
    return 0;
    }

  return 1;
}

//----------------------------------------------------------------------------
void vtkPVSource::SetParametersParent(vtkKWWidget *parent)
{
  if (this->ParametersParent == parent)
    {
    return;
    }
  if (this->ParametersParent)
    {
    vtkErrorMacro("Cannot reparent properties.");
    return;
    }
  this->ParametersParent = parent;
  parent->Register(this);
}


  

//---------------------------------------------------------------------------
void vtkPVSource::SetNumberOfPVInputs(int num)
{
  int idx;
  vtkPVSource** inputs;

  // in case nothing has changed.
  if (num == this->NumberOfPVInputs)
    {
    return;
    }
  
  // Allocate new arrays.
  inputs = new vtkPVSource* [num];

  // Initialize with NULLs.
  for (idx = 0; idx < num; ++idx)
    {
    inputs[idx] = NULL;
    }

  // Copy old inputs
  for (idx = 0; idx < num && idx < this->NumberOfPVInputs; ++idx)
    {
    inputs[idx] = this->PVInputs[idx];
    }
  
  // delete the previous arrays
  if (this->PVInputs)
    {
    delete [] this->PVInputs;
    this->PVInputs = NULL;
    this->NumberOfPVInputs = 0;
    }
  
  // Set the new array
  this->PVInputs = inputs;
  
  this->NumberOfPVInputs = num;
  this->Modified();
}


//---------------------------------------------------------------------------
void vtkPVSource::SetNthPVInput(int idx, vtkPVSource *pvs)
{
  if (idx < 0)
    {
    vtkErrorMacro(<< "SetNthPVInput: " << idx << ", cannot set input. ");
    return;
    }
  
  // Expand array if necessary.
  if (idx >= this->NumberOfPVInputs)
    {
    this->SetNumberOfPVInputs(idx + 1);
    }
  
  // Does this change anything?  Yes, it keeps the object from being modified.
  if (pvs == this->PVInputs[idx])
    {
    return;
    }
  
  if (this->PVInputs[idx])
    {
    this->PVInputs[idx]->RemovePVConsumer(this);
    this->PVInputs[idx]->UnRegister(this);
    this->PVInputs[idx] = NULL;
    }
  
  if (pvs)
    {
    pvs->Register(this);
    pvs->AddPVConsumer(this);
    this->PVInputs[idx] = pvs;
    }

  this->Modified();
}

//---------------------------------------------------------------------------
void vtkPVSource::RemoveAllPVInputs()
{
  if ( this->PVInputs )
    {
    int idx;
    for (idx = 0; idx < this->NumberOfPVInputs; ++idx)
      {
      this->SetNthPVInput(idx, NULL);
      }

    delete [] this->PVInputs;
    this->PVInputs = NULL;
    this->NumberOfPVInputs = 0;

    // Make sure to disconnect all VTK filters as well
    vtkPVApplication *pvApp = this->GetPVApplication();
    if (pvApp)
      {
      vtkPVProcessModule* pm = pvApp->GetProcessModule();

      int numSources = this->GetNumberOfVTKSources();
      vtkClientServerStream& stream = pm->GetStream();
      for (idx = 0; idx < numSources; ++idx)
        {
        vtkClientServerID sourceID = this->GetVTKSourceID(idx);
        stream << vtkClientServerStream::Invoke 
               << sourceID << "RemoveAllInputs"
               << vtkClientServerStream::End;
        }
      pm->SendStream(vtkProcessModule::DATA_SERVER);

      if (this->Proxy)
        {
        vtkSMPropertyIterator* iter = this->Proxy->NewPropertyIterator();
        iter->Begin();
        while (!iter->IsAtEnd())
          {
          vtkSMInputProperty* ip = vtkSMInputProperty::SafeDownCast(
            iter->GetProperty());
          if (ip)
            {
            ip->RemoveAllProxies();
            }
          iter->Next();
          }
        iter->Delete();
        }

      }

    this->Modified();
    }
}

//---------------------------------------------------------------------------
vtkPVSource *vtkPVSource::GetNthPVInput(int idx)
{
  if (idx >= this->NumberOfPVInputs)
    {
    return NULL;
    }
  
  return (vtkPVSource *)(this->PVInputs[idx]);
}


//---------------------------------------------------------------------------
void vtkPVSource::SetPVOutput(vtkPVData *pvd)
{  
  // Does this change anything?  Yes, it keeps the object from being modified.
  if (pvd == this->PVOutput)
    {
    return;
    }
  
  if (this->PVOutput)
    {
    // Manage backward pointer.
    this->PVOutput->SetPVSource(this);
    this->PVOutput->UnRegister(this);
    this->PVOutput = NULL;
    }
  
  if (pvd)
    {
    pvd->Register(this);
    this->PVOutput = pvd;
    // Manage backward pointer.
    pvd->SetPVSource(this);
    }

  this->Modified();
}

//----------------------------------------------------------------------------
void vtkPVSource::SaveInBatchScript(ofstream *file)
{
  // This should not be needed, but We can check anyway.
  if (this->VisitedFlag)
    {
    return;
    }

  this->SaveFilterInBatchScript(file);
  // Add the mapper, actor, scalar bar actor ...
  this->GetPVOutput()->SaveInBatchScript(file);
}

//----------------------------------------------------------------------------
void vtkPVSource::SaveFilterInBatchScript(ofstream *file)
{
  int i;

  // Detect special sources we do not handle yet.
  if (this->GetSourceClassName() == NULL)
    {
    return;
    }

  // This is the recursive part.
  this->VisitedFlag = 1;
  // Loop through all of the inputs
  for (i = 0; i < this->NumberOfPVInputs; ++i)
    {
    if (this->PVInputs[i] && this->PVInputs[i]->GetVisitedFlag() != 2)
      {
      this->PVInputs[i]->SaveInBatchScript(file);
      }
    }
  
  // Save the object in the script.
  *file << "\n"; 
  const char* module_group = 0;
  vtkPVSource* input0 = this->GetPVInput(0);
  if (input0)
    {
    module_group = "filters";
    }
  else
    {
    module_group = "sources";
    }
  *file << "set pvTemp" <<  this->GetVTKSourceID(0)
        << " [$proxyManager NewProxy " << module_group << " " 
        << this->GetModuleName() << "]"
        << endl;
  *file << "  $proxyManager RegisterProxy " << module_group << " pvTemp" 
        << this->GetVTKSourceID(0) << " $pvTemp" << this->GetVTKSourceID(0)
        << endl;
  *file << "  $pvTemp" << this->GetVTKSourceID(0) << " UnRegister {}" << endl;

  this->SetInputsInBatchScript(file);

  // Let the PVWidgets set up the object.
  vtkCollectionIterator *it = this->Widgets->NewIterator();
  vtkPVWidget *pvw;
  it->InitTraversal();
  while ( !it->IsDoneWithTraversal() )
    {
    pvw = static_cast<vtkPVWidget*>(it->GetObject());
    pvw->SaveInBatchScript(file);
    it->GoToNextItem();
    }
  it->Delete();

  *file << "  $pvTemp" <<  this->GetVTKSourceID(0)
        << " UpdateVTKObjects" << endl;
}

//----------------------------------------------------------------------------
void vtkPVSource::SaveState(ofstream *file)
{
  int i, numWidgets;
  vtkPVWidget *pvw;

  // Detect if this source is in Glyph sourcesm and already exists.
  if (this->GetTraceReferenceCommand())
    {
    *file << "set kw(" << this->GetTclName() << ") [$kw(" 
          << this->GetTraceReferenceObject()->GetTclName() << ") " 
          << this->GetTraceReferenceCommand() << "]\n";
    return;
    }

  // Detect special sources we do not handle yet.
//  if (this->GetVTKSource() == NULL)
//    {
//    return;
//    }

  // This should not be needed, but We can check anyway.
  if (this->VisitedFlag)
    {
    return;
    }

  // This is the recursive part.
  this->VisitedFlag = 1;
  // Loop through all of the inputs
  for (i = 0; i < this->NumberOfPVInputs; ++i)
    {
    if (this->PVInputs[i] && this->PVInputs[i]->GetVisitedFlag() != 2)
      {
      this->PVInputs[i]->SaveState(file);
      }
    }
  
  // We have to set the first input as the current source,
  // because CreatePVSource uses it as default input.
  // We may not have a input menu to set it for us.
  if (this->GetPVInput(0))
    {
    *file << "$kw(" << this->GetPVWindow()->GetTclName() << ") "
          << "SetCurrentPVSourceCallback $kw("
          << this->GetPVInput(0)->GetTclName() << ")\n";
    }

  // Save the object in the script.
  *file << "set kw(" << this->GetTclName() << ") "
        << "[$kw(" << this->GetPVWindow()->GetTclName() << ") "
        << "CreatePVSource " << this->GetModuleName() << "]" << endl;

  // Let the PVWidgets set up the object.
  numWidgets = this->Widgets->GetNumberOfItems();
  vtkCollectionIterator *it = this->Widgets->NewIterator();
  it->InitTraversal();
  for (i = 0; i < numWidgets; i++)
    {
    pvw = static_cast<vtkPVWidget*>(it->GetObject());
    pvw->SaveState(file);
    it->GoToNextItem();
    }
  it->Delete();
  
  // Call accept.
  *file << "$kw(" << this->GetTclName() << ") AcceptCallback" << endl;

  // What about visibility?  
  // A second pass to set visibility?
  // Can we disable rendering and changing input visibility?

  // Let the output set its state.
  this->GetPVOutput()->SaveState(file);
}

//----------------------------------------------------------------------------
void vtkPVSource::SetInputsInBatchScript(ofstream *file)
{
  int numInputs = this->GetNumberOfPVInputs();

  for (int inpIdx=0; inpIdx<numInputs; inpIdx++)
    {
    // Just PVInput 0 for now.
    vtkPVSource* pvs = this->GetNthPVInput(inpIdx);

    // Set the VTK reference to the new input.
    const char* inputName;
    vtkPVInputProperty* ip=0;
    if (this->VTKMultipleInputsFlag)
      {
      ip = this->GetInputProperty(0);
      }
    else
      {
      ip = this->GetInputProperty(inpIdx);
      }

    if (ip)
      {
      inputName = ip->GetName();
      }
    else
      {
      vtkErrorMacro("No input property defined, setting to default.");
      inputName = "Input";
      }

    *file << "  [$pvTemp" <<  this->GetVTKSourceID(0) 
          << " GetProperty " << inputName << "]"
          << " AddProxy $pvTemp" << pvs->GetVTKSourceID(0)
          << endl;
    }

}


//----------------------------------------------------------------------------
void vtkPVSource::AddPVWidget(vtkPVWidget *pvw)
{
  char str[512];
  this->Widgets->AddItem(pvw);

  if (pvw->GetTraceName() == NULL)
    {
    vtkWarningMacro("TraceName not set. Widget class: " 
                    << pvw->GetClassName());
    return;
    }

  pvw->SetTraceReferenceObject(this);
  sprintf(str, "GetPVWidget {%s}", pvw->GetTraceName());
  pvw->SetTraceReferenceCommand(str);
  pvw->Select();
}


//----------------------------------------------------------------------------
vtkIdType vtkPVSource::GetNumberOfInputProperties()
{
  return this->InputProperties->GetNumberOfItems();
}

//----------------------------------------------------------------------------
vtkPVInputProperty* vtkPVSource::GetInputProperty(int idx)
{
  return static_cast<vtkPVInputProperty*>(this->InputProperties->GetItemAsObject(idx));
}


//----------------------------------------------------------------------------
vtkPVInputProperty* vtkPVSource::GetInputProperty(const char* name)
{
  int idx, num;
  vtkPVInputProperty *inProp;
  
  num = this->GetNumberOfInputProperties();
  for (idx = 0; idx < num; ++idx)
    {
    inProp = static_cast<vtkPVInputProperty*>(this->GetInputProperty(idx));
    if (strcmp(name, inProp->GetName()) == 0)
      {
      return inProp;
      }
    }

  // Propery has not been created yet.  Create and  save one.
  inProp = vtkPVInputProperty::New();
  inProp->SetName(name);
  this->InputProperties->AddItem(inProp);
  inProp->Delete();

  return inProp;
}


//----------------------------------------------------------------------------
void vtkPVSource::SetAcceptButtonColorToModified()
{
  if (this->AcceptButtonRed)
    {
    return;
    }
  this->AcceptButtonRed = 1;
  this->Script("%s configure -background #17b27e",
               this->AcceptButton->GetWidgetName());
  this->Script("%s configure -activebackground #17b27e",
               this->AcceptButton->GetWidgetName());
}

//----------------------------------------------------------------------------
void vtkPVSource::SetAcceptButtonColorToUnmodified()
{
  if (!this->AcceptButtonRed)
    {
    return;
    }
  this->AcceptButtonRed = 0;

#ifdef _WIN32
  this->Script("%s configure -background [lindex [%s configure -background] 3]",
               this->AcceptButton->GetWidgetName(),
               this->AcceptButton->GetWidgetName());
  this->Script("%s configure -activebackground "
               "[lindex [%s configure -activebackground] 3]",
               this->AcceptButton->GetWidgetName(),
               this->AcceptButton->GetWidgetName());
#else
  this->Script("%s configure -background #ccc",
               this->AcceptButton->GetWidgetName());
  this->Script("%s configure -activebackground #eee",
               this->AcceptButton->GetWidgetName());
#endif
}

//----------------------------------------------------------------------------
vtkPVWidget* vtkPVSource::GetPVWidget(const char *name)
{
  vtkObject *o;
  vtkPVWidget *pvw;
  this->Widgets->InitTraversal();
  
  while ( (o = this->Widgets->GetNextItemAsObject()) )
    {
    pvw = vtkPVWidget::SafeDownCast(o);
    if (pvw && pvw->GetTraceName() && strcmp(pvw->GetTraceName(), name) == 0)
      {
      return pvw;
      }
    }

  return NULL;
}

//----------------------------------------------------------------------------
vtkPVRenderView* vtkPVSource::GetPVRenderView()
{
  return vtkPVRenderView::SafeDownCast(this->View);
}

//----------------------------------------------------------------------------
int vtkPVSource::GetNumberOfProcessorsValid()
{
  vtkPVApplication *pvApp = this->GetPVApplication();
  if (!pvApp)
    {
    return 0;
    }
  int numProcs = pvApp->GetProcessModule()->GetNumberOfPartitions();
  
  switch (this->VTKMultipleProcessFlag)
    {
    case 0:
      if (numProcs > 1)
        {
        return 0;
        }
      break;
    case 1:
      if (numProcs == 1)
        {
        return 0;
        }
      break;
    case 2:
      break;
    default:
      return 0;
    }
  return 1;
}

//----------------------------------------------------------------------------
int vtkPVSource::CloneAndInitialize(int makeCurrent, vtkPVSource*& clone)
{

  int retVal = this->ClonePrototypeInternal(clone);
  if (retVal != VTK_OK)
    {
    return retVal;
    }

  vtkPVSource *current = this->GetPVWindow()->GetCurrentPVSource();
  retVal = clone->InitializeClone(current, makeCurrent);


  if (retVal != VTK_OK)
    {
    clone->Delete();
    clone = 0;
    return retVal;
    }

  // Accept button is always red when a source is first created.
  clone->SetAcceptButtonColorToModified();

  return VTK_OK;
}

//----------------------------------------------------------------------------
void vtkPVSource::RegisterProxy(const char* sourceList, vtkPVSource* clone)
{
  const char* module_group = 0;
  if (sourceList)
    {
    if (strcmp(sourceList, "GlyphSources") == 0)
      {
        module_group = "glyph_sources";
      }
    else
      {
      module_group = sourceList;
      }
    }
  else if (this->GetNumberOfInputProperties() > 0)
    {
    module_group = "filters";
    }
  else
    {
    module_group = "sources";
    }

  vtkSMProxyManager* proxm = vtkSMObject::GetProxyManager();
  proxm->RegisterProxy(module_group, clone->GetName(), clone->Proxy);

}

//----------------------------------------------------------------------------
int vtkPVSource::ClonePrototype(vtkPVSource*& clone)
{
  return this->ClonePrototypeInternal(clone);
}

//----------------------------------------------------------------------------
int vtkPVSource::ClonePrototypeInternal(vtkPVSource*& clone)
{
  int idx;

  clone = 0;

  vtkPVSource* pvs = this->NewInstance();
  // Copy properties
  pvs->SetApplication(this->GetApplication());
  pvs->SetReplaceInput(this->ReplaceInput);
  pvs->SetParametersParent(this->ParametersParent);

  pvs->SetShortHelp(this->GetShortHelp());
  pvs->SetLongHelp(this->GetLongHelp());
  pvs->SetVTKMultipleInputsFlag(this->GetVTKMultipleInputsFlag());

  pvs->SetSourceClassName(this->SourceClassName);
  // Copy the VTK input stuff.
  vtkIdType numItems = this->GetNumberOfInputProperties();
  vtkIdType id;
  vtkPVInputProperty *inProp;
  for(id=0; id<numItems; id++)
    {
    inProp = this->GetInputProperty(id);
    pvs->GetInputProperty(inProp->GetName())->Copy(inProp);
    }

  pvs->SetModuleName(this->ModuleName);

  vtkPVApplication* pvApp = vtkPVApplication::SafeDownCast(pvs->GetApplication());
  if (!pvApp)
    {
    vtkErrorMacro("vtkPVApplication is not set properly. Aborting clone.");
    pvs->Delete();
    return VTK_ERROR;
    }

  // Create a (unique) name for the PVSource.
  // Beware: If two prototypes have the same name, the name 
  // will not be unique.
  // Does this name really have to be unique?
  char tclName[1024];
  if (this->Name && this->Name[0] != '\0')
    { 
    sprintf(tclName, "%s%d", this->Name, this->PrototypeInstanceCount);
    }
  else
    {
    vtkErrorMacro("The prototype must have a name. Cloning aborted.");
    pvs->Delete();
    return VTK_ERROR;
    }
  pvs->SetName(tclName);

  vtkPVProcessModule* pm = pvApp->GetProcessModule();
  
  // We need oue source for each part.
  int numSources = 1;
  // Set the input if necessary.
  if (this->GetNumberOfInputProperties() > 0)
    {
    vtkPVSource *input = this->GetPVWindow()->GetCurrentPVSource();
    numSources = input->GetNumberOfParts();
    }
  // If the VTK filter takes multiple inputs (vtkAppendPolyData)
  // then we create only one filter with each part as an input.
  if (this->GetVTKMultipleInputsFlag())
    {
    numSources = 1;
    }

  vtkSMProxyManager* proxm = vtkSMObject::GetProxyManager();

  const char* module_group = 0;
  if (this->GetNumberOfInputProperties() > 0)
    {
    module_group = "filters";
    }
  else
    {
    module_group = "sources";
    }

  pvs->Proxy = vtkSMSourceProxy::SafeDownCast(
    proxm->NewProxy(module_group, this->GetModuleName()));
  if (!pvs->Proxy)
    {
    vtkErrorMacro("Can not create " 
                  << (this->GetModuleName()?this->GetModuleName():"(nil)")
                  << " : " << module_group);
    pvs->Delete();
    return VTK_ERROR;
    }
  pvs->Proxy->CreateVTKObjects(numSources);

  for (idx = 0; idx < numSources; ++idx)
    {
    // Create a vtkSource
    vtkClientServerID sourceId = pvs->Proxy->GetID(idx);

    // Keep track of how long each filter takes to execute.
    ostrstream filterName_with_warning_C4701;
    filterName_with_warning_C4701 << "Execute " << this->SourceClassName
                                  << " id: " << sourceId.ID << ends;
    vtkClientServerStream start;
    start << vtkClientServerStream::Invoke << pm->GetProcessModuleID() 
          << "LogStartEvent" << filterName_with_warning_C4701.str()
          << vtkClientServerStream::End;
    vtkClientServerStream end;
    end << vtkClientServerStream::Invoke << pm->GetProcessModuleID() 
        << "LogEndEvent" << filterName_with_warning_C4701.str()
        << vtkClientServerStream::End;
    delete[] filterName_with_warning_C4701.str();

    pm->GetStream() << vtkClientServerStream::Invoke << sourceId 
                    << "AddObserver"
                    << "StartEvent"
                    << start
                    << vtkClientServerStream::End;
    pm->GetStream() << vtkClientServerStream::Invoke << sourceId 
                    << "AddObserver"
                    << "EndEvent"
                    << end
                    << vtkClientServerStream::End;
    pm->GetStream() << vtkClientServerStream::Invoke << pm->GetProcessModuleID()
                    << "RegisterProgressEvent"
                    << sourceId << sourceId.ID
                    << vtkClientServerStream::End;
    
    }
  pm->SendStream(vtkProcessModule::DATA_SERVER);
  pvs->SetView(this->GetPVWindow()->GetMainView());

  pvs->PrototypeInstanceCount = this->PrototypeInstanceCount;
  this->PrototypeInstanceCount++;

  vtkArrayMap<vtkPVWidget*, vtkPVWidget*>* widgetMap =
    vtkArrayMap<vtkPVWidget*, vtkPVWidget*>::New();


  // Copy all widgets
  vtkPVWidget *pvWidget, *clonedWidget;
  vtkCollectionIterator *it = this->Widgets->NewIterator();
  it->InitTraversal();

  int i;
  for (i = 0; i < this->Widgets->GetNumberOfItems(); i++)
    {
    pvWidget = static_cast<vtkPVWidget*>(it->GetObject());
    clonedWidget = pvWidget->ClonePrototype(pvs, widgetMap);
    pvs->AddPVWidget(clonedWidget);
    clonedWidget->Delete();
    it->GoToNextItem();
    }
  widgetMap->Delete();
  it->Delete();

  clone = pvs;
  return VTK_OK;
}


//----------------------------------------------------------------------------
int vtkPVSource::InitializeClone(vtkPVSource* input,
                                 int makeCurrent)

{
  // Set the input if necessary.
  if (this->GetNumberOfInputProperties() > 0)
    {
    // Set the VTK reference to the new input.
    vtkPVInputProperty* ip = this->GetInputProperty(0);
    if (ip)
      {
      this->SetPVInput(ip->GetName(), 0, input);
      }
    else
      {
      this->SetPVInput("Input",  0, input);
      }
    }

  // Create the properties frame etc.
  this->CreateProperties();

  if (makeCurrent)
    {
    this->GetPVWindow()->SetCurrentPVSourceCallback(this);
    } 

  return VTK_OK;
}



//----------------------------------------------------------------------------
// This stuff used to be in Initialize clone.
// I want to initialize the output after accept is called.
int vtkPVSource::InitializeData()
{
  vtkPVApplication* pvApp = this->GetPVApplication();
  vtkPVData* pvd;

  // Create the output.
  pvd = vtkPVData::New();
  pvd->SetPVApplication(pvApp);

  this->Proxy->CreateParts();

  unsigned int numParts = this->Proxy->GetNumberOfParts();
  for (unsigned int i=0; i<numParts; i++)
    {
    vtkSMPart* smpart = this->Proxy->GetPart(i); 
    this->AddPart(smpart);
    }

  this->SetPVOutput(pvd);
  pvd->Delete();

  return VTK_OK;
}

//----------------------------------------------------------------------------
void vtkPVSource::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
  os << indent << "Initialized: " << (this->Initialized?"yes":"no") << endl;
  os << indent << "Name: " << (this->Name ? this->Name : "none") << endl;
  os << indent << "LongHelp: " << (this->LongHelp ? this->LongHelp : "none") 
     << endl;
  os << indent << "ShortHelp: " << (this->ShortHelp ? this->ShortHelp : "none") 
     << endl;
  os << indent << "Description: " 
     << (this->Label ? this->Label : "none") << endl;
  os << indent << "ModuleName: " << (this->ModuleName?this->ModuleName:"none")
     << endl;
  os << indent << "MenuName: " << (this->MenuName?this->MenuName:"none")
     << endl;
  os << indent << "AcceptButton: " << this->GetAcceptButton() << endl;
  os << indent << "DeleteButton: " << this->GetDeleteButton() << endl;
  os << indent << "MainParameterFrame: " << this->GetMainParameterFrame() 
     << endl;
  os << indent << "DescriptionFrame: " << this->DescriptionFrame 
     << endl;
  os << indent << "Notebook: " << this->GetNotebook() << endl;
  os << indent << "NumberOfPVInputs: " << this->GetNumberOfPVInputs() << endl;
  os << indent << "ParameterFrame: " << this->GetParameterFrame() << endl;
  os << indent << "ParametersParent: " << this->GetParametersParent() << endl;
  os << indent << "ReplaceInput: " << this->GetReplaceInput() << endl;
  os << indent << "View: " << this->GetView() << endl;
  os << indent << "VisitedFlag: " << this->GetVisitedFlag() << endl;
  os << indent << "Widgets: " << this->GetWidgets() << endl;
  os << indent << "IsPermanent: " << this->IsPermanent << endl;
  os << indent << "SourceClassName: " 
     << (this->SourceClassName?this->SourceClassName:"null") << endl;
  os << indent << "HideParametersPage: " << this->HideParametersPage << endl;
  os << indent << "HideDisplayPage: " << this->HideDisplayPage << endl;
  os << indent << "HideInformationPage: " << this->HideInformationPage << endl;
  os << indent << "ToolbarModule: " << this->ToolbarModule << endl;

  os << indent << "PVOutput: " << this->PVOutput << endl;
  os << indent << "NumberOfPVConsumers: " << this->GetNumberOfPVConsumers() << endl;
  os << indent << "NumberOfParts: " << this->GetNumberOfParts() << endl;

  os << indent << "VTKMultipleInputsFlag: " << this->VTKMultipleInputsFlag << endl;
  os << indent << "VTKMultipleProcessFlag: " << this->VTKMultipleProcessFlag
     << endl;
  os << indent << "InputProperties: \n";
  vtkIndent i2 = indent.GetNextIndent();
  int num, idx;
  num = this->GetNumberOfInputProperties();
  for (idx = 0; idx < num; ++idx)
    {
    this->GetInputProperty(idx)->PrintSelf(os, i2);
    }
  os << indent << "NumberOfOutputsInformation: "
     << this->NumberOfOutputsInformation << endl;
  os << indent << "SourceGrabbed: " << (this->SourceGrabbed?"on":"off") << endl;
  os << indent << "Proxy: ";
  if (this->Proxy)
    {
    os << endl;
    this->Proxy->PrintSelf(os, indent.GetNextIndent());
    }
  else
    {
    os << "(none)" << endl;
    }
}
