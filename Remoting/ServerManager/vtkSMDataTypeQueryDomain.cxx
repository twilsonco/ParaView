// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkSMDataTypeQueryDomain.h"

#include "vtkObjectFactory.h"
#include "vtkPVDataInformation.h"
#include "vtkSMIntVectorProperty.h"

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSMDataTypeQueryDomain);

//----------------------------------------------------------------------------
vtkSMDataTypeQueryDomain::vtkSMDataTypeQueryDomain() = default;

//----------------------------------------------------------------------------
vtkSMDataTypeQueryDomain::~vtkSMDataTypeQueryDomain() = default;

//----------------------------------------------------------------------------
void vtkSMDataTypeQueryDomain::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "InputDataType: " << this->InputDataType << endl;
}

//---------------------------------------------------------------------------
int vtkSMDataTypeQueryDomain::IsInDomain(vtkSMProperty* property)
{
  if (this->IsOptional)
  {
    return 1;
  }

  return vtkSMIntVectorProperty::SafeDownCast(property) != nullptr;
}

//----------------------------------------------------------------------------
void vtkSMDataTypeQueryDomain::Update(vtkSMProperty* vtkNotUsed(prop))
{
  vtkSMProperty* input = this->GetRequiredProperty("Input");
  if (!input)
  {
    vtkErrorMacro("Missing require property 'Input'. Update failed.");
    return;
  }
  vtkPVDataInformation* dataInfo = this->GetInputDataInformation("Input");
  if (!dataInfo)
  {
    return;
  }

  const int inputDataType = dataInfo->IsCompositeDataSet() ? dataInfo->GetCompositeDataSetType()
                                                           : dataInfo->GetDataSetType();
  if (inputDataType != this->InputDataType)
  {
    this->InputDataType = inputDataType;
    vtkSMIntVectorProperty::SafeDownCast(this->GetProperty())->SetElement(0, this->InputDataType);
    this->DomainModified();
  }
}

//----------------------------------------------------------------------------
int vtkSMDataTypeQueryDomain::SetDefaultValues(vtkSMProperty* prop, bool use_unchecked_values)
{
  auto placeHolderProp = vtkSMIntVectorProperty::SafeDownCast(prop);
  if (!placeHolderProp)
  {
    vtkErrorMacro("Property is not a vtkSMIntVectorProperty.");
    return 0;
  }
  if (use_unchecked_values)
  {
    placeHolderProp->SetUncheckedElement(0, this->InputDataType);
  }
  else
  {
    placeHolderProp->SetElement(0, this->InputDataType);
  }
  return this->Superclass::SetDefaultValues(prop, use_unchecked_values);
}
