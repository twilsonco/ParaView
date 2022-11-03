/*=========================================================================

   Program: ParaView
   Module:    pqSourcesMenuReaction.cxx

   Copyright (c) 2005,2006 Sandia Corporation, Kitware Inc.
   All rights reserved.

   ParaView is a free software; you can redistribute it and/or modify it
   under the terms of the ParaView license version 1.2.

   See License_v1.2.txt for the full ParaView license.
   A copy of this license can be obtained by contacting
   Kitware Inc.
   28 Corporate Drive
   Clifton Park, NY 12065
   USA

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

========================================================================*/
#include "pqSourcesMenuReaction.h"

#include "pqActiveObjects.h"
#include "pqCollaborationManager.h"
#include "pqCoreUtilities.h"
#include "pqObjectBuilder.h"
#include "pqProxyGroupMenuManager.h"
#include "pqServer.h"
#include "pqUndoStack.h"
#include "vtkPVDataInformation.h"
#include "vtkPVMemoryUseInformation.h"
#include "vtkPVServerInformation.h"
#include "vtkPVSystemConfigInformation.h"
#include "vtkPVXMLElement.h"
#include "vtkSMCollaborationManager.h"
#include "vtkSMDataTypeDomain.h"
#include "vtkSMProxy.h"
#include "vtkSMSession.h"
#include "vtkSMSessionProxyManager.h"

#include <QCoreApplication>

//-----------------------------------------------------------------------------
pqSourcesMenuReaction::pqSourcesMenuReaction(pqProxyGroupMenuManager* menuManager)
  : Superclass(menuManager)
{
  QObject::connect(menuManager, SIGNAL(triggered(const QString&, const QString&)), this,
    SLOT(onTriggered(const QString&, const QString&)));

  pqActiveObjects* activeObjects = &pqActiveObjects::instance();
  QObject::connect(
    activeObjects, SIGNAL(serverChanged(pqServer*)), this, SLOT(updateEnableState()));
  QObject::connect(menuManager, SIGNAL(menuPopulated()), this, SLOT(updateEnableState()));
  QObject::connect(pqApplicationCore::instance(), SIGNAL(updateMasterEnableState(bool)), this,
    SLOT(updateEnableState()));
  this->updateEnableState();
}

//-----------------------------------------------------------------------------
void pqSourcesMenuReaction::updateEnableState()
{
  pqActiveObjects* activeObjects = &pqActiveObjects::instance();
  this->updateEnableState(
    activeObjects->activeServer() != nullptr && activeObjects->activeServer()->isMaster());
}
//-----------------------------------------------------------------------------
void pqSourcesMenuReaction::updateEnableState(bool enabled)
{
  pqProxyGroupMenuManager* mgr = static_cast<pqProxyGroupMenuManager*>(this->parent());
  mgr->setEnabled(enabled);
  Q_FOREACH (QAction* action, mgr->actions())
  {
    action->setEnabled(enabled);
  }
}

//-----------------------------------------------------------------------------
bool pqSourcesMenuReaction::warnOnCreate(
  const QString& xmlgroup, const QString& xmlname, pqServer* server)
{
  server = server ? server : pqActiveObjects::instance().activeServer();
  if (server)
  {
    vtkSMSessionProxyManager* pxm = server->proxyManager();
    vtkSMProxy* prototype =
      pxm->GetPrototypeProxy(xmlgroup.toUtf8().data(), xmlname.toUtf8().data());
    if (!prototype)
    {
      // skip the prompt.
      return true;
    }

    if (vtkPVXMLElement* hints = prototype->GetHints()
        ? prototype->GetHints()->FindNestedElementByName("WarnOnCreate")
        : nullptr)
    {
      bool shouldWarn = true;

      // Recover active output port data type and check against the provided dataset type names
      pqOutputPort* port = pqActiveObjects::instance().activePort();
      if (port)
      {
        // Look for a DataTypeDomain condition on the warn
        vtkPVXMLElement* dataTypeDomain = hints->FindNestedElementByName("DataTypeDomain");
        if (dataTypeDomain)
        {
          vtkNew<vtkSMDataTypeDomain> smDomain;
          smDomain->ParseXMLAttributes(dataTypeDomain);
          if (!smDomain->IsInDomain(port->getSourceProxy(), port->getPortNumber()))
          {
            shouldWarn = false;
          }
        }

        // Recover memory usage condition
        vtkPVXMLElement* memoryUsage = hints->FindNestedElementByName("MemoryUsage");
        if (shouldWarn && memoryUsage)
        {
          // Recover memory information
          vtkNew<vtkPVMemoryUseInformation> infos;
          vtkNew<vtkPVSystemConfigInformation> serverInfos;

          auto session = server->session();
          session->GatherInformation(vtkPVSession::SERVERS, infos, 0);
          session->GatherInformation(vtkPVSession::SERVERS, serverInfos, 0);

          long long worstRemainingMemory = std::numeric_limits<long long>::infinity();
          for (size_t rank = 0; rank < infos->GetSize(); ++rank)
          {
            long long hostMemoryUse = infos->GetHostMemoryUse(rank);
            long long hostMemoryAvailable = serverInfos->GetHostMemoryAvailable(rank);
            if (hostMemoryAvailable <= 0)
            {
              continue;
            }
            long long remainingMemory = hostMemoryAvailable - hostMemoryUse;
            if (remainingMemory < worstRemainingMemory)
            {
              worstRemainingMemory = remainingMemory;
            }
          }

          // Compare with the input size
          vtkTypeInt64 inputSize = port->getDataInformation()->GetMemorySize();
          double relative = std::stod(memoryUsage->GetAttributeOrDefault("relative", "1"));
          if (inputSize * relative < worstRemainingMemory)
          {
            shouldWarn = false;
          }
        }
      }

      if (shouldWarn)
      {
        // Recover Text and Title
        QString title;
        QString text;

        // PARAVIEW_DEPRECATED_IN_5_12_0
#if !defined(VTK_LEGACY_REMOVE)
        title = hints->GetAttributeOrEmpty("title");
        text = hints->GetCharacterData();
#if !defined(VTK_LEGACY_SILENT)
        if (!title.isEmpty())
        {
          qWarning("WarnOnCreate title property has been deprecated in ParaView 5.12, "
                   "add a Text element with a title property instead");
        }
        if (!text.trimmed().isEmpty())
        {
          qWarning("WarnOnCreate character data has been deprecated in ParaView 5.12, "
                   "add a Text element with character data instead.");
        }
#endif
#endif
        vtkPVXMLElement* textElement = hints->FindNestedElementByName("Text");
        if (textElement)
        {
          title = textElement->GetAttributeOrEmpty("title");
          text = textElement->GetCharacterData();
        }

        if (title.isEmpty())
        {
          title = tr("Are you sure?");
        }
        if (text.isEmpty())
        {
          text = tr("Creating '%1'. Do you want to continue?")
                   .arg(QCoreApplication::translate("ServerManagerXML", prototype->GetXMLLabel()));
        }

        return pqCoreUtilities::promptUser(QString("WarnOnCreate/%1/%2").arg(xmlgroup).arg(xmlname),
          QMessageBox::Information, pqProxy::rstToHtml(title), pqProxy::rstToHtml(text),
          QMessageBox::Yes | QMessageBox::No | QMessageBox::Save);
      }
    }
  }
  return true;
}

//-----------------------------------------------------------------------------
pqPipelineSource* pqSourcesMenuReaction::createSource(const QString& group, const QString& name)
{
  if (pqSourcesMenuReaction::warnOnCreate(group, name))
  {
    pqApplicationCore* core = pqApplicationCore::instance();
    pqObjectBuilder* builder = core->getObjectBuilder();
    BEGIN_UNDO_SET(tr("Create '%1'").arg(name));
    pqPipelineSource* source =
      builder->createSource(group, name, pqActiveObjects::instance().activeServer());
    END_UNDO_SET();
    return source;
  }
  return nullptr;
}
