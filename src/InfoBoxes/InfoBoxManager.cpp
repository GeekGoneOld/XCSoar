/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2011 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}

*/

#include "InfoBoxes/InfoBoxManager.hpp"
#include "InfoBoxes/InfoBoxWindow.hpp"
#include "InfoBoxes/InfoBoxLayout.hpp"
#include "Protection.hpp"
#include "InfoBoxes/Content/Factory.hpp"
#include "InfoBoxes/Content/Base.hpp"
#include "Profile/InfoBoxConfig.hpp"
#include "InputEvents.hpp"
#include "Screen/Blank.hpp"
#include "Screen/Layout.hpp"
#include "Screen/Fonts.hpp"
#include "Screen/Graphics.hpp"
#include "Hardware/Battery.hpp"
#include "MainWindow.hpp"
#include "Appearance.hpp"
#include "Language.hpp"
#include "DataField/Enum.hpp"
#include "DataField/ComboList.hpp"
#include "Dialogs/Dialogs.h"
#include "Profile/InfoBoxConfig.hpp"
#include "Interface.hpp"

#include <assert.h>
#include <stdio.h>

#include <algorithm>

namespace InfoBoxManager
{
  /** the window for displaying infoboxes full-screen */
  InfoBoxFullWindow full_window;

  unsigned GetCurrentType(unsigned box);
  void SetCurrentType(unsigned box, char type);

  void DisplayInfoBox();
  void InfoBoxDrawIfDirty();
  int GetFocused();

  int GetInfoBoxBorder(unsigned i);
}

static bool InfoBoxesDirty = false;
static bool InfoBoxesHidden = false;

InfoBoxWindow *InfoBoxes[InfoBoxPanelConfig::MAX_INFOBOXES];

InfoBoxManagerConfig infoBoxManagerConfig;

static InfoBoxLook info_box_look;

void
InfoBoxFullWindow::on_paint(Canvas &canvas)
{
  canvas.clear_white();

  for (unsigned i = 0; i < InfoBoxLayout::numInfoWindows; i++) {
    // JMW TODO: make these calculated once only.
    int x, y;
    int rx, ry;
    int rw;
    int rh;
    double fw, fh;

    if (Layout::landscape) {
      rw = 84;
      rh = 68;
    } else {
      rw = 120;
      rh = 80;
    }

    fw = rw / (double)InfoBoxLayout::ControlWidth;
    fh = rh / (double)InfoBoxLayout::ControlHeight;

    double f = std::min(fw, fh);
    rw = (int)(f * InfoBoxLayout::ControlWidth);
    rh = (int)(f * InfoBoxLayout::ControlHeight);

    if (Layout::landscape) {
      rx = i % 3;
      ry = i / 3;

      x = (rw + 4) * rx;
      y = (rh + 3) * ry;
    } else {
      rx = i % 2;
      ry = i / 4;

      x = (rw) * rx;
      y = (rh) * ry;
    }

    InfoBoxes[i]->PaintInto(canvas, IBLSCALE(x), IBLSCALE(y),
                            IBLSCALE(rw), IBLSCALE(rh));
  }
}

// TODO locking
void
InfoBoxManager::Hide()
{
  InfoBoxesHidden = true;

  for (unsigned i = 0; i < InfoBoxLayout::numInfoWindows; i++)
    InfoBoxes[i]->fast_hide();

  full_window.hide();
}

void
InfoBoxManager::Show()
{
  InfoBoxesHidden = false;

  for (unsigned i = 0; i < InfoBoxLayout::numInfoWindows; i++)
    InfoBoxes[i]->show();
}

int
InfoBoxManager::GetFocused()
{
  for (unsigned i = 0; i < InfoBoxLayout::numInfoWindows; i++)
    if (InfoBoxes[i]->has_focus())
      return i;

  return -1;
}

void
InfoBoxManager::Event_Select(int i)
{
  int InfoFocus = GetFocused();

  if (InfoFocus < 0) {
    InfoFocus = (i >= 0 ? 0 : InfoBoxLayout::numInfoWindows - 1);
  } else {
    InfoFocus += i;

    if (InfoFocus < 0 || (unsigned)InfoFocus >= InfoBoxLayout::numInfoWindows)
      InfoFocus = -1;
  }

  if (InfoFocus >= 0)
    XCSoarInterface::main_window.map.set_focus();
  else
    InfoBoxes[i]->set_focus();
}

unsigned
InfoBoxManager::GetCurrentPanel()
{
  if (XCSoarInterface::SettingsMap().EnableAuxiliaryInfo)
    return PANEL_AUXILIARY;
  else if (XCSoarInterface::main_window.map.GetDisplayMode() == dmCircling)
    return PANEL_CIRCLING;
  else if (XCSoarInterface::main_window.map.GetDisplayMode() == dmFinalGlide)
    return PANEL_FINAL_GLIDE;
  else
    return PANEL_CRUISE;
}

const TCHAR*
InfoBoxManager::GetPanelName(unsigned panelIdx)
{
  return infoBoxManagerConfig.panel[panelIdx].name;
}

const TCHAR*
InfoBoxManager::GetCurrentPanelName()
{
  return GetPanelName(GetCurrentPanel());
}

unsigned
InfoBoxManager::GetType(unsigned box, unsigned panelIdx)
{
  assert(box < InfoBoxPanelConfig::MAX_INFOBOXES);
  assert(panelIdx < InfoBoxManagerConfig::MAX_INFOBOX_PANELS);

  return infoBoxManagerConfig.panel[panelIdx].infoBoxID[box];
}

unsigned
InfoBoxManager::GetCurrentType(unsigned box)
{
  unsigned retval = GetType(box, GetCurrentPanel());
  return std::min(InfoBoxFactory::NUM_TYPES - 1, retval);
}

bool
InfoBoxManager::IsEmpty(unsigned panelIdx)
{
  return infoBoxManagerConfig.panel[panelIdx].IsEmpty();
}

void
InfoBoxManager::SetType(unsigned i, unsigned type, unsigned panelIdx)
{
  assert(i < InfoBoxPanelConfig::MAX_INFOBOXES);
  assert(panelIdx < InfoBoxManagerConfig::MAX_INFOBOX_PANELS);

  if ((unsigned int) type != infoBoxManagerConfig.panel[panelIdx].infoBoxID[i]) {
    infoBoxManagerConfig.panel[panelIdx].infoBoxID[i] = type;
    infoBoxManagerConfig.panel[panelIdx].modified = true;
  }
}

void
InfoBoxManager::SetCurrentType(unsigned box, unsigned type)
{
  SetType(box, type, GetCurrentPanel());
}

void
InfoBoxManager::Event_Change(int i)
{
  int j = 0, k;

  int InfoFocus = GetFocused();
  if (InfoFocus < 0)
    return;

  k = GetCurrentType(InfoFocus);
  if (i > 0)
    j = InfoBoxFactory::GetNext(k);
  else if (i < 0)
    j = InfoBoxFactory::GetPrevious(k);

  // TODO code: if i==0, go to default or reset

  SetCurrentType(InfoFocus, (unsigned) j);

  InfoBoxes[InfoFocus]->UpdateContent();
  Paint();
}

void
InfoBoxManager::DisplayInfoBox()
{
  if (InfoBoxesHidden)
    return;

  int DisplayType[InfoBoxPanelConfig::MAX_INFOBOXES];
  static bool first = true;
  static int DisplayTypeLast[InfoBoxPanelConfig::MAX_INFOBOXES];

  // JMW note: this is updated every GPS time step

  for (unsigned i = 0; i < InfoBoxLayout::numInfoWindows; i++) {
    // All calculations are made in a separate thread. Slow calculations
    // should apply to the function DoCalculationsSlow()
    // Do not put calculations here!

    DisplayType[i] = GetCurrentType(i);

    bool needupdate = ((DisplayType[i] != DisplayTypeLast[i]) || first);

    if (needupdate) {
      InfoBoxes[i]->SetTitle(gettext(InfoBoxFactory::GetCaption(DisplayType[i])));
      InfoBoxes[i]->SetContentProvider(InfoBoxFactory::Create(DisplayType[i]));
      InfoBoxes[i]->SetID(i);
    }

    InfoBoxes[i]->UpdateContent();

    DisplayTypeLast[i] = DisplayType[i];
  }

  Paint();

  first = false;
}

void
InfoBoxManager::ProcessKey(InfoBoxContent::InfoBoxKeyCodes keycode)
{
  int focus = GetFocused();
  if (focus < 0)
    return;

  if (InfoBoxes[focus] != NULL)
    InfoBoxes[focus]->HandleKey(keycode);

  InputEvents::HideMenu();

  SetDirty();

  // emulate update to trigger calculations
  TriggerGPSUpdate();

  ResetDisplayTimeOut();
}

InfoBoxContent::InfoBoxDlgContent*
InfoBoxManager::GetInfoBoxDlgContent(const int id)
{
  if (id < 0)
    return NULL;

  if (InfoBoxes[id] != NULL)
    return InfoBoxes[id]->GetInfoBoxDlgContent();

  return NULL;
}

void
InfoBoxManager::ProcessQuickAccess(const int id, const TCHAR *Value)
{
  if (id < 0)
    return;

  // do approciate action
  if (InfoBoxes[id] != NULL)
    InfoBoxes[id]->HandleQuickAccess(Value);

  SetDirty();
  // emulate update to trigger calculations
  TriggerGPSUpdate();
  ResetDisplayTimeOut();
}

bool
InfoBoxManager::HasFocus()
{
  return GetFocused() >= 0;
}

void
InfoBoxManager::InfoBoxDrawIfDirty()
{
  // No need to redraw map or infoboxes if screen is blanked.
  // This should save lots of battery power due to CPU usage
  // of drawing the screen

  if (InfoBoxesDirty && !XCSoarInterface::SettingsMap().ScreenBlanked) {
    DisplayInfoBox();
    InfoBoxesDirty = false;
  }
}

void
InfoBoxManager::SetDirty()
{
  InfoBoxesDirty = true;
}

void
InfoBoxManager::ProcessTimer()
{
  static Validity last;

  if (XCSoarInterface::Basic().Connected.modified(last)) {
    SetDirty();
    last = XCSoarInterface::Basic().Connected;
  }

  InfoBoxDrawIfDirty();
}

void
InfoBoxManager::Paint()
{
  if (!InfoBoxLayout::fullscreen) {
    full_window.hide();
  } else {
    full_window.invalidate();
    full_window.show();
  }
}

int
InfoBoxManager::GetInfoBoxBorder(unsigned i)
{
  if (Appearance.InfoBoxBorder == apIbTab)
    return 0;

  unsigned border = 0;

  switch (InfoBoxLayout::InfoBoxGeometry) {
  case InfoBoxLayout::ibTop4Bottom4:
    if (i < 4)
      border |= BORDERBOTTOM;
    else
      border |= BORDERTOP;

    if (i != 3 && i != 7)
      border |= BORDERRIGHT;
    break;

  case InfoBoxLayout::ibBottom8:
    border |= BORDERTOP;

    if (i != 3 && i != 7)
      border |= BORDERRIGHT;
    break;

  case InfoBoxLayout::ibTop8:
    border |= BORDERBOTTOM;

    if (i != 3 && i != 7)
      border |= BORDERRIGHT;
    break;

  case InfoBoxLayout::ibLeft4Right4:
    if (i != 3 && i != 7)
      border |= BORDERBOTTOM;

    if (i < 4)
      border |= BORDERRIGHT;
    else
      border |= BORDERLEFT;
    break;

  case InfoBoxLayout::ibLeft8:
    if (i != 3 && i != 7)
      border |= BORDERBOTTOM;

    border |= BORDERRIGHT;
    break;

  case InfoBoxLayout::ibRight8:
    if (i != 3 && i != 7)
      border |= BORDERBOTTOM;

    border |= BORDERLEFT;
    break;

  case InfoBoxLayout::ibGNav:
  case InfoBoxLayout::ibRight12:
    if (i != 0)
      border |= BORDERTOP;
    if (i < 6)
      border |= BORDERLEFT|BORDERRIGHT;
    break;

  case InfoBoxLayout::ibSquare:
    break;
  }

  return border;
}

void
InfoBoxManager::Create(RECT rc)
{
  info_box_look.value.fg_color
    = info_box_look.title.fg_color
    = info_box_look.comment.fg_color
    = Appearance.InverseInfoBox ? Color::WHITE : Color::BLACK;
  info_box_look.background_brush.set(Appearance.InverseInfoBox
                                     ? Color::BLACK : Color::WHITE);

  Color border_color = Color(80, 80, 80);
  info_box_look.border_pen.set(InfoBoxWindow::BORDER_WIDTH, border_color);
  info_box_look.selector_pen.set(IBLSCALE(1) + 2,
                                 info_box_look.value.fg_color);

  info_box_look.value.font = &Fonts::InfoBox;
  info_box_look.title.font = &Fonts::Title;
  info_box_look.comment.font = &Fonts::Title;
  info_box_look.small_font = &Fonts::InfoBoxSmall;

  info_box_look.colors[0] = border_color;
  info_box_look.colors[1] = Appearance.InverseInfoBox
    ? Graphics::inv_redColor : Color::RED;
  info_box_look.colors[2] = Appearance.InverseInfoBox
    ? Graphics::inv_blueColor : Color::BLUE;
  info_box_look.colors[3] = Appearance.InverseInfoBox
    ? Graphics::inv_greenColor : Color::GREEN;
  info_box_look.colors[4] = Appearance.InverseInfoBox
    ? Graphics::inv_yellowColor : Color::YELLOW;
  info_box_look.colors[5] = Appearance.InverseInfoBox
    ? Graphics::inv_magentaColor : Color::MAGENTA;

  WindowStyle style;
  style.hide();
  full_window.set(XCSoarInterface::main_window, rc.left, rc.top,
                  rc.right - rc.left, rc.bottom - rc.top);

  // create infobox windows
  for (unsigned i = 0; i < InfoBoxLayout::numInfoWindows; i++) {
    int xoff, yoff, sizex, sizey;
    InfoBoxLayout::GetInfoBoxPosition(i, rc, &xoff, &yoff, &sizex, &sizey);
    int Border = GetInfoBoxBorder(i);

    InfoBoxes[i] = new InfoBoxWindow(XCSoarInterface::main_window,
                                     xoff, yoff, sizex, sizey,
                                     Border, info_box_look);
  }
}

void
InfoBoxManager::Destroy()
{
  for (unsigned i = 0; i < InfoBoxLayout::numInfoWindows; i++)
    delete (InfoBoxes[i]);

  full_window.reset();
}

static const ComboList *info_box_combo_list;

static void
OnInfoBoxHelp(unsigned item)
{
  int type = (*info_box_combo_list)[item].DataFieldIndex;

  TCHAR caption[100];
  _stprintf(caption, _T("%s: %s"), _("InfoBox"), gettext(InfoBoxFactory::GetName(type)));

  const TCHAR* text = InfoBoxFactory::GetDescription(type);
  if (text)
    dlgHelpShowModal(XCSoarInterface::main_window, caption, gettext(text));
  else
    dlgHelpShowModal(XCSoarInterface::main_window, caption,
                     _("No help available on this item"));
}

void
InfoBoxManager::ShowDlgInfoBox(const int id)
{
  if (GetInfoBoxDlgContent(id))
    dlgInfoBoxAccessShowModal(XCSoarInterface::main_window, id);
  else SetupFocused(id);
}

void
InfoBoxManager::SetupFocused(const int id)
{
  int i;

  if (id < 0) i = GetFocused();
  else i = id;

  if (i < 0)
    return;

  const unsigned panel = GetCurrentPanel();
  int old_type = GetType(i, panel);

  /* create a fake WndProperty for dlgComboPicker() */
  /* XXX reimplement properly */

  DataFieldEnum *dfe = new DataFieldEnum(NULL);
  for (unsigned i = 0; i < InfoBoxFactory::NUM_TYPES; i++)
    dfe->addEnumText(gettext(InfoBoxFactory::GetName(i)));
  dfe->Sort(0);
  dfe->Set(old_type);

  ComboList *list = dfe->CreateComboList();
  delete dfe;

  /* let the user select */

  TCHAR caption[20];
  _stprintf(caption, _T("%s: %d"), _("InfoBox"), i + 1);
  info_box_combo_list = list;
  int result = ComboPicker(XCSoarInterface::main_window, caption, *list,
                           OnInfoBoxHelp);
  if (result < 0) {
    delete list;
    return;
  }

  /* was there a modification? */

  int new_type = (*list)[result].DataFieldIndex;
  delete list;
  if (new_type == old_type)
    return;

  /* yes: apply and save it */

  SetType(i, new_type, panel);
  DisplayInfoBox();
  Profile::SetInfoBoxManagerConfig(infoBoxManagerConfig);
}
