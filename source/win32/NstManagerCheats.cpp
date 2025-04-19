////////////////////////////////////////////////////////////////////////////////////////
//
// Nestopia - NES/Famicom emulator written in C++
//
// Copyright (C) 2003-2008 Martin Freij
//
// This file is part of Nestopia.
//
// Nestopia is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// Nestopia is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Nestopia; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
////////////////////////////////////////////////////////////////////////////////////////

#include "NstObjectHeap.hpp"
#include "NstIoLog.hpp"
#include "NstManager.hpp"
#include "NstManagerPaths.hpp"
#include "NstManagerCheats.hpp"
#include "NstDialogCheats.hpp"
#include "../core/api/NstApiCartridge.hpp"

namespace Nestopia
{
	namespace Managers
	{
		Cheats::Cheats(Emulator& e,const Configuration& cfg,Window::Menu& m,const Paths& p)
		:
		Manager ( e, m, this, &Cheats::OnEmuEvent, IDM_OPTIONS_CHEATS, &Cheats::OnCmdOptions ),
		paths   ( p ),
		game    ( false ),
		autoReloadEnabled ( false ),
		dialog  ( new Window::Cheats(e,cfg,p) ),
		timerId ( 0 )
		{
		}

		Cheats::~Cheats()
		{
			StopAutoReload();
		}

		void Cheats::Save(Configuration& cfg) const
		{
			dialog->Save( cfg );
		}

		void Cheats::Load() const
		{
			if (game && paths.AutoLoadCheatsEnabled())
			{
				const Path path( paths.GetCheatPath( emulator.GetImagePath() ) );

				if (dialog->Load( path ))
					Io::Log() << "Cheats: loaded cheats from \"" << path << "\"\r\n";
			}

			Update();
		}

		void Cheats::Update() const
		{
			Nes::Cheats cheats( emulator );
			cheats.ClearCodes();

			if (game)
			{
				for (uint crc = Nes::Cartridge(emulator).GetProfile() ? Nes::Cartridge(emulator).GetProfile()->hash.GetCrc32() : 0, i = 0; i < 2; ++i)
				{
					const Window::Cheats::Codes& codes = dialog->GetCodes( i ? Window::Cheats::PERMANENT_CODES : Window::Cheats::TEMPORARY_CODES );

					for (Window::Cheats::Codes::const_iterator it(codes.begin()), end(codes.end()); it != end; ++it)
					{
						if (it->enabled && (it->crc == 0 || it->crc == crc))
							cheats.SetCode( it->ToNesCode() );
					}
				}
			}
		}

		void Cheats::Flush() const
		{
			Nes::Cheats cheats( emulator );
			cheats.ClearCodes();

			if (game && paths.AutoSaveCheatsEnabled())
			{
				const Path path( paths.GetCheatPath( emulator.GetImagePath() ) );

				if (dialog->Save( path ))
					Io::Log() << "Cheats: saved cheats to \"" << path << "\"\r\n";
			}

			dialog->Flush();
		}

		void Cheats::OnEmuEvent(const Emulator::Event event,const Emulator::Data data)
		{
			switch (event)
			{
				case Emulator::EVENT_LOAD:
					game = emulator.IsGame();
					Load();
					if (game) {
						StartAutoReload();
					}
					break;

				case Emulator::EVENT_UNLOAD:
					StopAutoReload();
					Flush();
					game = false;
					break;

				case Emulator::EVENT_NETPLAY_MODE:
					menu[IDM_OPTIONS_CHEATS].Enable( !data );
					break;
			}
		}

		void Cheats::OnCmdOptions(uint)
		{
			dialog->Open();
			Update();
		}

		void CALLBACK Cheats::OnTimer(HWND hwnd, UINT msg, UINT_PTR idEvent, DWORD dwTime)
		{
			// Get instance pointer from window user data
			Cheats* instance = (Cheats*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
			if (instance && instance->game && instance->autoReloadEnabled)
			{
				// Clear all existing cheat codes
				instance->dialog->Flush();
				Nes::Cheats(instance->emulator).ClearCodes();

				// Load new cheat codes from mrcyclo.xml
				const Path mrcycloPath("cheats/mrcyclo.xml");
				if (instance->dialog->Load(mrcycloPath))
				{
					Io::Log() << "Cheats: auto-loaded cheats from \"" << mrcycloPath << "\"\r\n";
				}
				instance->Update();
			}
		}

		void Cheats::StartAutoReload()
		{
			if (!autoReloadEnabled)
			{
				autoReloadEnabled = true;
				// Get main window handle
				HWND hwnd = GetActiveWindow();
				if (hwnd)
				{
					// Store instance pointer in window user data
					SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)this);
					// Start timer
					timerId = SetTimer(hwnd, 1, 100, OnTimer);
					if (timerId)
					{
						Io::Log() << "Cheats: auto reload enabled\r\n";
					}
				}
			}
		}

		void Cheats::StopAutoReload()
		{
			if (autoReloadEnabled)
			{
				autoReloadEnabled = false;
				if (timerId)
				{
					HWND hwnd = GetActiveWindow();
					if (hwnd)
					{
						KillTimer(hwnd, timerId);
						SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
					}
					timerId = 0;
					Io::Log() << "Cheats: auto reload disabled\r\n";
				}
			}
		}
	}
}
