// SilentAim.cpp - bottega host for the paid silent-aim modules.
// Mirrors paid Aim::Render silent dispatch: one engine hook slot shared by
// raycast + magic bullet, independent writer threads for viewport/mouse/phantom.
#include "SilentAim.h"
#include "RaycastSilent.h"
#include "ViewportSilent.h"
#include "MouseSilent.h"
#include "PhantomSilent.h"

namespace SilentAim {
using namespace Cheat::Features;

void DisarmAll();

void TickMethod(int method)
{
    const bool wants_hook = (method == 2 || method == 3); // raycast || magic bullet
    if (wants_hook)
    {
        if (!RaycastSilent::Ready())
        {
            RaycastSilent::Ensure(true);
        }
    }
    else
    {
        RaycastSilent::Ensure(false);
    }
}

void Drive(int method, const paidm::Vector3& world, bool fire, bool force_mb)
{
    if (!fire)
    {
        DisarmAll();
        return;
    }

    switch (method)
    {
    case 0: // viewport
        MouseSilent::SetActive(false, {});
        PhantomSilent::SetActive(false, {});
        RaycastSilent::SetActive(false, {}, false);
        ViewportSilent::SetActive(true, world);
        break;

    case 1: // mouse
        ViewportSilent::SetActive(false, {});
        PhantomSilent::SetActive(false, {});
        RaycastSilent::SetActive(false, {}, false);
        MouseSilent::SetActive(true, world);
        break;

    case 2: // raycast
        ViewportSilent::SetActive(false, {});
        MouseSilent::SetActive(false, {});
        PhantomSilent::SetActive(false, {});
        RaycastSilent::SetActive(true, world, force_mb);
        break;

    case 3: // magic bullet (wallbang, camera-ray skip inside the stub)
        ViewportSilent::SetActive(false, {});
        MouseSilent::SetActive(false, {});
        PhantomSilent::SetActive(false, {});
        RaycastSilent::SetActive(true, world, true);
        break;

    case 4: // phantom
        ViewportSilent::SetActive(false, {});
        MouseSilent::SetActive(false, {});
        RaycastSilent::SetActive(false, {}, false);
        PhantomSilent::SetActive(true, world);
        break;

    default:
        DisarmAll();
        break;
    }
}

void DisarmAll()
{
    RaycastSilent::SetActive(false, {}, false);
    ViewportSilent::SetActive(false, {});
    MouseSilent::SetActive(false, {});
    PhantomSilent::SetActive(false, {});
}

void Shutdown()
{
    DisarmAll();
    RaycastSilent::Ensure(false);
    ViewportSilent::Shutdown();
    MouseSilent::Shutdown();
    PhantomSilent::Shutdown();
}

} // namespace SilentAim