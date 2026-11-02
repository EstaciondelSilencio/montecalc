// MonteCalc - UI helpers
#include "ui.hpp"
#include "util.hpp"

#include <com/sun/star/awt/MessageBoxType.hpp>
#include <com/sun/star/awt/PushButtonType.hpp>
#include <com/sun/star/awt/Toolkit.hpp>
#include <com/sun/star/awt/XCheckBox.hpp>
#include <com/sun/star/awt/XControl.hpp>
#include <com/sun/star/awt/XControlContainer.hpp>
#include <com/sun/star/awt/XDialog.hpp>
#include <com/sun/star/awt/XMessageBoxFactory.hpp>
#include <com/sun/star/awt/XTextComponent.hpp>
#include <com/sun/star/awt/XToolkit.hpp>
#include <com/sun/star/awt/XWindowPeer.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XNameContainer.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/lang/XMultiComponentFactory.hpp>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;

namespace montecalc {

void showMessage(const Reference<uno::XComponentContext>& ctx, const Reference<frame::XFrame>& frame,
                 const std::string& title, const std::string& text, bool error) {
    Reference<awt::XToolkit> tk(awt::Toolkit::create(ctx), UNO_QUERY_THROW);
    Reference<awt::XMessageBoxFactory> mbf(tk, UNO_QUERY_THROW);
    Reference<awt::XWindowPeer> parent;
    if (frame.is()) parent.set(frame->getContainerWindow(), UNO_QUERY);
    auto box = mbf->createMessageBox(parent,
                                     error ? awt::MessageBoxType_ERRORBOX : awt::MessageBoxType_INFOBOX,
                                     1 /* OK */, ou(title), ou(text));
    box->execute();
    Reference<lang::XComponent>(box, UNO_QUERY_THROW)->dispose();
}

bool showForm(const Reference<uno::XComponentContext>& ctx, const std::string& title,
              std::vector<FormField>& fields) {
    auto smgr = ctx->getServiceManager();
    Reference<lang::XMultiServiceFactory> dlgModelFactory(
        smgr->createInstanceWithContext("com.sun.star.awt.UnoControlDialogModel", ctx), UNO_QUERY_THROW);
    Reference<beans::XPropertySet> dlgProps(dlgModelFactory, UNO_QUERY_THROW);
    const sal_Int32 rowH = 14, width = 200;
    sal_Int32 height = 10 + rowH * sal_Int32(fields.size()) + 24;
    dlgProps->setPropertyValue("PositionX", Any(sal_Int32(60)));
    dlgProps->setPropertyValue("PositionY", Any(sal_Int32(60)));
    dlgProps->setPropertyValue("Width", Any(width));
    dlgProps->setPropertyValue("Height", Any(height));
    dlgProps->setPropertyValue("Title", Any(ou(title)));
    Reference<container::XNameContainer> cont(dlgModelFactory, UNO_QUERY_THROW);

    auto add = [&](const char* type, const OUString& name, sal_Int32 x, sal_Int32 y, sal_Int32 w,
                   sal_Int32 h) {
        Reference<beans::XPropertySet> m(dlgModelFactory->createInstance(OUString::createFromAscii(type)),
                                         UNO_QUERY_THROW);
        m->setPropertyValue("PositionX", Any(x));
        m->setPropertyValue("PositionY", Any(y));
        m->setPropertyValue("Width", Any(w));
        m->setPropertyValue("Height", Any(h));
        cont->insertByName(name, Any(m));
        return m;
    };

    for (size_t i = 0; i < fields.size(); ++i) {
        sal_Int32 y = 8 + rowH * sal_Int32(i);
        OUString id = "f" + OUString::number(sal_Int32(i));
        if (fields[i].isCheck) {
            auto m = add("com.sun.star.awt.UnoControlCheckBoxModel", id, 8, y, width - 16, 12);
            m->setPropertyValue("Label", Any(ou(fields[i].label)));
            m->setPropertyValue("State", Any(sal_Int16(fields[i].checked ? 1 : 0)));
        } else {
            auto l = add("com.sun.star.awt.UnoControlFixedTextModel", "l" + id, 8, y + 2, 110, 10);
            l->setPropertyValue("Label", Any(ou(fields[i].label)));
            auto m = add("com.sun.star.awt.UnoControlEditModel", id, 122, y, width - 130, 12);
            m->setPropertyValue("Text", Any(ou(fields[i].value)));
        }
    }
    auto ok = add("com.sun.star.awt.UnoControlButtonModel", "ok", width - 108, height - 20, 48, 14);
    ok->setPropertyValue("Label", Any(OUString("OK")));
    ok->setPropertyValue("PushButtonType", Any(sal_Int16(awt::PushButtonType_OK)));
    ok->setPropertyValue("DefaultButton", Any(true));
    auto cancel = add("com.sun.star.awt.UnoControlButtonModel", "cancel", width - 56, height - 20, 48, 14);
    cancel->setPropertyValue("Label", Any(OUString("Cancel")));
    cancel->setPropertyValue("PushButtonType", Any(sal_Int16(awt::PushButtonType_CANCEL)));

    Reference<awt::XControl> dlg(
        smgr->createInstanceWithContext("com.sun.star.awt.UnoControlDialog", ctx), UNO_QUERY_THROW);
    dlg->setModel(Reference<awt::XControlModel>(dlgModelFactory, UNO_QUERY_THROW));
    dlg->createPeer(Reference<awt::XToolkit>(awt::Toolkit::create(ctx), UNO_QUERY_THROW), Reference<awt::XWindowPeer>());
    Reference<awt::XDialog> xd(dlg, UNO_QUERY_THROW);
    bool accepted = xd->execute() == 1;
    if (accepted) {
        Reference<awt::XControlContainer> cc(dlg, UNO_QUERY_THROW);
        for (size_t i = 0; i < fields.size(); ++i) {
            auto c = cc->getControl("f" + OUString::number(sal_Int32(i)));
            if (fields[i].isCheck)
                fields[i].checked = Reference<awt::XCheckBox>(c, UNO_QUERY_THROW)->getState() == 1;
            else
                fields[i].value = u8(Reference<awt::XTextComponent>(c, UNO_QUERY_THROW)->getText());
        }
    }
    Reference<lang::XComponent>(dlg, UNO_QUERY_THROW)->dispose();
    return accepted;
}

} // namespace montecalc
