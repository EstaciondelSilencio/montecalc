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
#include <com/sun/star/awt/XListBox.hpp>
#include <com/sun/star/awt/XMessageBoxFactory.hpp>
#include <com/sun/star/awt/XTextComponent.hpp>
#include <com/sun/star/awt/XToolkit.hpp>
#include <com/sun/star/awt/XWindowPeer.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/container/XNameContainer.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/lang/XMultiComponentFactory.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;

namespace montecalc {

void showMessage(const Reference<uno::XComponentContext>& ctx, const Reference<frame::XFrame>& frame, const std::string& title,
                 const std::string& text, bool error) {
    Reference<awt::XToolkit> tk(awt::Toolkit::create(ctx), UNO_QUERY_THROW);
    Reference<awt::XMessageBoxFactory> mbf(tk, UNO_QUERY_THROW);
    Reference<awt::XWindowPeer> parent;
    if (frame.is()) parent.set(frame->getContainerWindow(), UNO_QUERY);
    auto box = mbf->createMessageBox(parent, error ? awt::MessageBoxType_ERRORBOX : awt::MessageBoxType_INFOBOX, 1,
                                     ou(title), ou(text));
    box->execute();
    Reference<lang::XComponent>(box, UNO_QUERY_THROW)->dispose();
}

bool showForm(const Reference<uno::XComponentContext>& ctx, const std::string& title, std::vector<FormField>& fields, int width) {
    auto smgr = ctx->getServiceManager();
    Reference<lang::XMultiServiceFactory> mf(
        smgr->createInstanceWithContext("com.sun.star.awt.UnoControlDialogModel", ctx), UNO_QUERY_THROW);
    Reference<beans::XPropertySet> dp(mf, UNO_QUERY_THROW);
    const sal_Int32 rowH = 14;
    sal_Int32 y = 8;
    std::vector<sal_Int32> rowY;
    for (const auto& f : fields) {
        rowY.push_back(y);
        y += f.lines > 1 ? 10 + 10 * f.lines : rowH;
    }
    sal_Int32 height = y + 24;
    dp->setPropertyValue("PositionX", Any(sal_Int32(60)));
    dp->setPropertyValue("PositionY", Any(sal_Int32(40)));
    dp->setPropertyValue("Width", Any(sal_Int32(width)));
    dp->setPropertyValue("Height", Any(height));
    dp->setPropertyValue("Title", Any(ou(title)));
    Reference<container::XNameContainer> cont(mf, UNO_QUERY_THROW);
    auto add = [&](const char* type, const OUString& name, sal_Int32 x, sal_Int32 yy, sal_Int32 w, sal_Int32 h) {
        Reference<beans::XPropertySet> m(mf->createInstance(OUString::createFromAscii(type)), UNO_QUERY_THROW);
        m->setPropertyValue("PositionX", Any(x));
        m->setPropertyValue("PositionY", Any(yy));
        m->setPropertyValue("Width", Any(w));
        m->setPropertyValue("Height", Any(h));
        cont->insertByName(name, Any(m));
        return m;
    };
    const sal_Int32 labelW = width * 45 / 100;
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& f = fields[i];
        OUString id = "f" + OUString::number(sal_Int32(i));
        sal_Int32 yy = rowY[i];
        if (f.isCheck) {
            auto m = add("com.sun.star.awt.UnoControlCheckBoxModel", id, 8, yy, width - 16, 12);
            m->setPropertyValue("Label", Any(ou(f.label)));
            m->setPropertyValue("State", Any(sal_Int16(f.checked ? 1 : 0)));
        } else if (f.lines > 1) {
            if (!f.label.empty()) {
                auto l = add("com.sun.star.awt.UnoControlFixedTextModel", "l" + id, 8, yy, width - 16, 10);
                l->setPropertyValue("Label", Any(ou(f.label)));
            }
            auto m = add("com.sun.star.awt.UnoControlEditModel", id, 8, yy + 10, width - 16, 10 * f.lines - 2);
            m->setPropertyValue("MultiLine", Any(true));
            m->setPropertyValue("VScroll", Any(true));
            m->setPropertyValue("ReadOnly", Any(f.readOnly));
            m->setPropertyValue("Text", Any(ou(f.value)));
        } else {
            auto l = add("com.sun.star.awt.UnoControlFixedTextModel", "l" + id, 8, yy + 2, labelW - 4, 10);
            l->setPropertyValue("Label", Any(ou(f.label)));
            if (!f.options.empty()) {
                auto m = add("com.sun.star.awt.UnoControlListBoxModel", id, labelW + 8, yy, width - labelW - 16, 12);
                Sequence<OUString> items(sal_Int32(f.options.size()));
                sal_Int16 sel = 0;
                for (size_t k = 0; k < f.options.size(); ++k) {
                    items.getArray()[k] = ou(f.options[k]);
                    if (f.options[k] == f.value) sel = sal_Int16(k);
                }
                m->setPropertyValue("StringItemList", Any(items));
                m->setPropertyValue("Dropdown", Any(true));
                m->setPropertyValue("SelectedItems", Any(seq<sal_Int16>({sel})));
            } else {
                auto m = add("com.sun.star.awt.UnoControlEditModel", id, labelW + 8, yy, width - labelW - 16, 12);
                m->setPropertyValue("Text", Any(ou(f.value)));
                m->setPropertyValue("ReadOnly", Any(f.readOnly));
            }
        }
    }
    auto ok = add("com.sun.star.awt.UnoControlButtonModel", "ok", width - 108, height - 20, 48, 14);
    ok->setPropertyValue("Label", Any(OUString("OK")));
    ok->setPropertyValue("PushButtonType", Any(sal_Int16(awt::PushButtonType_OK)));
    ok->setPropertyValue("DefaultButton", Any(true));
    auto cancel = add("com.sun.star.awt.UnoControlButtonModel", "cancel", width - 56, height - 20, 48, 14);
    cancel->setPropertyValue("Label", Any(OUString("Cancel")));
    cancel->setPropertyValue("PushButtonType", Any(sal_Int16(awt::PushButtonType_CANCEL)));

    Reference<awt::XControl> dlg(smgr->createInstanceWithContext("com.sun.star.awt.UnoControlDialog", ctx), UNO_QUERY_THROW);
    dlg->setModel(Reference<awt::XControlModel>(mf, UNO_QUERY_THROW));
    dlg->createPeer(Reference<awt::XToolkit>(awt::Toolkit::create(ctx), UNO_QUERY_THROW), Reference<awt::XWindowPeer>());
    bool accepted = Reference<awt::XDialog>(dlg, UNO_QUERY_THROW)->execute() == 1;
    if (accepted) {
        Reference<awt::XControlContainer> cc(dlg, UNO_QUERY_THROW);
        for (size_t i = 0; i < fields.size(); ++i) {
            auto c = cc->getControl("f" + OUString::number(sal_Int32(i)));
            if (fields[i].isCheck) fields[i].checked = Reference<awt::XCheckBox>(c, UNO_QUERY_THROW)->getState() == 1;
            else if (!fields[i].options.empty() && fields[i].lines <= 1)
                fields[i].value = u8(Reference<awt::XListBox>(c, UNO_QUERY_THROW)->getSelectedItem());
            else fields[i].value = u8(Reference<awt::XTextComponent>(c, UNO_QUERY_THROW)->getText());
        }
    }
    Reference<lang::XComponent>(dlg, UNO_QUERY_THROW)->dispose();
    return accepted;
}

} // namespace montecalc
