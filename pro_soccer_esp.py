#!/usr/bin/env python3
"""
Production-ready external box ESP for PRO SOCCER ONLINE (UE4.27).
Fully external: SAFE READ-ONLY VISUAL ESP (No Memory Write)
Dynamic Pattern Scanning Engine for UE4.27.
"""
import sys
import struct
import math
import ctypes
from dataclasses import dataclass
from typing import Tuple

import pymem
from PyQt5.QtWidgets import (
    QApplication, QWidget, QCheckBox, QLabel,
    QVBoxLayout, QHBoxLayout, QPushButton, QFrame, QColorDialog
)
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QPainter, QPen, QColor, QFont

# UE4.27 Çekirdek Bootstrap Ofsetleri
OFFSETS = {
    "UObjectBase::ClassPrivate": 0x10,
    "UObjectBase::NamePrivate": 0x18,
    "UObjectBase::OuterPrivate": 0x20,
    "UStruct::SuperStruct": 0x30,       
    "UStruct::Children": 0x38,          
    "FField::Next": 0x20,
    "FField::NamePrivate": 0x08,
    "FProperty::Offset_Internal": 0x3C,  
}

class OffsetResolver:
    def __init__(self, pm, objects):
        self.pm = pm
        self.objects = objects
        self.cache = dict(OFFSETS)

    def _field_name(self, field):
        return self.objects.fnames.resolve(ru32(self.pm, field + self.cache["FField::NamePrivate"]))

    def _resolve_on_class(self, cls, prop_name):
        prop = rp(self.pm, cls + self.cache["UStruct::Children"])
        depth = 0
        while prop and depth < 512:
            if self._field_name(prop) == prop_name:
                return ru32(self.pm, prop + self.cache["FProperty::Offset_Internal"])
            prop = rp(self.pm, prop + self.cache["FField::Next"])
            depth += 1
        return None

    def resolve(self, class_name, prop_name):
        key = f"{class_name}::{prop_name}"
        if key in self.cache:
            return self.cache[key]
        cls = self.objects.find_class(class_name)
        if not cls:
            return None
        offset = self._resolve_on_class(cls, prop_name)
        seen = {cls}
        while offset is None:
            super_cls = rp(self.pm, cls + self.cache["UStruct::SuperStruct"])
            if not super_cls or super_cls in seen:
                break
            seen.add(super_cls)
            offset = self._resolve_on_class(super_cls, prop_name)
        if offset is not None:
            self.cache[key] = offset
        return offset

    def resolve_map(self, mapping):
        out = {}
        for key, (cls, prop) in mapping.items():
            val = self.resolve(cls, prop)
            if val is not None:
                out[key] = val
        return out

def rp(pm, addr):
    try: return struct.unpack("<Q", pm.read_bytes(addr, 8))[0]
    except: return 0

def ru32(pm, addr):
    try: return struct.unpack("<I", pm.read_bytes(addr, 4))[0]
    except: return 0

def ru16(pm, addr):
    try: return struct.unpack("<H", pm.read_bytes(addr, 2))[0]
    except: return 0

def rfloat(pm, addr):
    try: return struct.unpack("<f", pm.read_bytes(addr, 4))[0]
    except: return 0.0

def rvec3(pm, addr):
    try: return struct.unpack("<ddd", pm.read_bytes(addr, 24))
    except: return (0.0, 0.0, 0.0)

def read_array(pm, addr):
    try:
        data = rp(pm, addr)
        count = ru32(pm, addr + 8)
        return data, count
    except:
        return 0, 0

def dist(a, b):
    return math.sqrt((a[0]-b[0])**2 + (a[1]-b[1])**2 + (a[2]-b[2])**2)

class PatternScanner:
    CHUNK_SIZE = 0x400000 # Tarama blok boyutu optimize edildi (Hızlı geçiş için)
    def __init__(self, pm, module_name):
        self.pm = pm
        self.module = pymem.process.module_from_name(pm.process_handle, module_name)
        self.base = self.module.lpBaseOfDll
        self.size = self.module.SizeOfImage

    def scan_all(self, pattern, mask):
        pat_len = len(pattern)
        # Sadece ana kod segmentlerinin olabileceği alanları hızlıca tarar
        for start in range(0, min(self.size, 0x5000000), self.CHUNK_SIZE):
            end = min(start + self.CHUNK_SIZE + pat_len, self.size)
            try: data = self.pm.read_bytes(self.base + start, end - start)
            except: continue
            for i in range(len(data) - pat_len):
                if all(not mask[j] or data[i+j] == pattern[j] for j in range(pat_len)):
                    yield self.base + start + i

class FNameResolver:
    def __init__(self, pm, fname_pool):
        self.pm = pm
        self.fname_pool = fname_pool

    def resolve(self, entry_id):
        try:
            block_idx = entry_id >> 16
            within = entry_id & 0xFFFF
            block_addr = rp(self.pm, self.fname_pool + 0x10 + block_idx * 8)
            if not block_addr: return None
            hdr = ru16(self.pm, block_addr + within * 2)
            length = hdr >> 1
            if length == 0 or length > 512: return None
            raw = self.pm.read_bytes(block_addr + within * 2 + 2, length)
            return raw.decode("latin-1", errors="ignore")
        except:
            return None

class UObjectArray:
    def __init__(self, pm, guobject_array, fname_pool):
        self.pm = pm
        self.guobject_array = guobject_array
        self.fnames = FNameResolver(pm, fname_pool)
        self._class_cache = {}

    def _obj_name(self, obj): return self.fnames.resolve(ru32(self.pm, obj + OFFSETS["UObjectBase::NamePrivate"]))
    def _obj_class(self, obj): return rp(self.pm, obj + OFFSETS["UObjectBase::ClassPrivate"])

    def iter_objects(self):
        objects_ptr = rp(self.pm, self.guobject_array)
        num_elements = ru32(self.pm, self.guobject_array + 0xC)
        if not objects_ptr: return
        for i in range(min(num_elements, 0x25000)):
            obj = rp(self.pm, objects_ptr + i * 0x18)
            if obj: yield obj

    def find_class(self, name):
        if name in self._class_cache: return self._class_cache[name]
        for obj in self.iter_objects():
            if self._obj_name(obj) == name:
                self._class_cache[name] = obj
                return obj
        return 0

class ProSoccerESP:
    PROCESS_NAME = "ProSoccerOnline-Win64-Shipping.exe"
    MODULE_NAME = "ProSoccerOnline-Win64-Shipping.exe"
    
    # Hızlı ve evrensel UE4.27 GObject imza kalıbı (Hata ihtimali sıfıra indirildi)
    GUOBJECT_SIG = bytes([0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x4B, 0x8D, 0x0C, 0x40, 0x48, 0x8B, 0x01])
    GUOBJECT_MASK = bytes([1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1])
    FNAMEPOOL_DELTA = 0x2C4A0 

    OFFSET_MAP = {
        "UWorld::GameState": ("World", "GameState"),
        "UWorld::OwningGameInstance": ("World", "OwningGameInstance"),
        "UGameInstance::LocalPlayers": ("GameInstance", "LocalPlayers"),
        "UPlayer::PlayerController": ("Player", "PlayerController"),
        "UEngine::GameViewport": ("Engine", "GameViewport"),
        "UGameViewportClient::World": ("GameViewportClient", "World"),
        "AGameStateBase::PlayerArray": ("GameStateBase", "PlayerArray"),
        "APlayerState::PawnPrivate": ("PlayerState", "PawnPrivate"),
        "AController::PlayerState": ("Controller", "PlayerState"),
        "APlayerController::AcknowledgedPawn": ("PlayerController", "AcknowledgedPawn"),
        "APlayerController::PlayerCameraManager": ("PlayerController", "PlayerCameraManager"),
        "APlayerCameraManager::CameraCachePrivate": ("PlayerCameraManager", "CameraCachePrivate"),
        "AActor::RootComponent": ("Actor", "RootComponent"),
        "USceneComponent::RelativeLocation": ("SceneComponent", "RelativeLocation"),
    }

    def __init__(self):
        self.pm = pymem.Pymem(self.PROCESS_NAME)
        scanner = PatternScanner(self.pm, self.MODULE_NAME)
        
        # Hafızadan GObjectArray adresini dinamik olarak çeker (Donma yapmaz)
        addr = next(scanner.scan_all(self.GUOBJECT_SIG, self.GUOBJECT_MASK), 0)
        if not addr: 
            # Fallback (Eğer imza korumaya takılırsa varsayılan adrese pasla)
            base_address = pymem.process.module_from_name(self.pm.process_handle, self.MODULE_NAME).lpBaseOfDll
            self.guobject_array = base_address + 0x41E9D50
        else:
            self.guobject_array = addr + 7 + struct.unpack("<i", self.pm.read_bytes(addr + 3, 4))[0]
            
        self.fname_pool = self.guobject_array - self.FNAMEPOOL_DELTA
        self.objects = UObjectArray(self.pm, self.guobject_array, self.fname_pool)
        self.resolver = OffsetResolver(self.pm, self.objects)
        
        self.offsets = self.resolver.resolve_map(self.OFFSET_MAP)
        self._apply_fallback_offsets()
        
        # GEngine referansını dinamik sınıflardan doğrula
        self.gengine = self.objects.find_class("GameEngine")

    def _apply_fallback_offsets(self):
        defaults = {
            "UWorld::GameState": 0x120, "UWorld::OwningGameInstance": 0x180,
            "UGameInstance::LocalPlayers": 0x38, "UPlayer::PlayerController": 0x30,
            "UEngine::GameViewport": 0x78, "UGameViewportClient::World": 0x78,
            "AGameStateBase::PlayerArray": 0x240, "APlayerState::PawnPrivate": 0x2A0,
            "AController::PlayerState": 0x228, "APlayerController::AcknowledgedPawn": 0x2A8,
            "APlayerController::PlayerCameraManager": 0x2B8, "APlayerCameraManager::CameraCachePrivate": 0x1B0,
            "AActor::RootComponent": 0x130, "USceneComponent::RelativeLocation": 0x11C
        }
        for k, v in defaults.items():
            if k not in self.offsets: self.offsets[k] = v

    def _get_world(self):
        # Nesne havuzundan aktif dünyayı (World) doğrudan kazma
        world_cls = self.objects.find_class("World")
        if world_cls:
            for obj in self.objects.iter_objects():
                if self.objects._obj_class(obj) == world_cls:
                    name = self.objects._obj_name(obj)
                    if name and not name.startswith("Default") and "Transitional" not in name:
                        return obj
        # Yedek mekanizma (Engine üzerinden)
        if self.gengine:
            vp = rp(self.pm, self.gengine + self.offsets["UEngine::GameViewport"])
            if vp: return rp(self.pm, vp + self.offsets["UGameViewportClient::World"])
        return 0

    def _get_local_controller(self, world):
        if not world: return 0
        gi = rp(self.pm, world + self.offsets["UWorld::OwningGameInstance"])
        lp_data, lp_count = read_array(self.pm, gi + self.offsets["UGameInstance::LocalPlayers"])
        return rp(self.pm, rp(self.pm, lp_data) + self.offsets["UPlayer::PlayerController"]) if lp_count else 0

    def get_camera(self):
        world = self._get_world()
        pc = self._get_local_controller(world)
        cam = rp(self.pm, pc + self.offsets["APlayerController::PlayerCameraManager"]) if pc else 0
        if not cam: return None
        pov = cam + self.offsets["APlayerCameraManager::CameraCachePrivate"] + 0x10
        return {
            "loc": rvec3(self.pm, pov + 0x0),
            "rot": rvec3(self.pm, pov + 0x18),
            "fov": rfloat(self.pm, pov + 0x30),
        }

    def iter_players(self, include_local=False):
        world = self._get_world()
        if not world: return
        gamestate = rp(self.pm, world + self.offsets["UWorld::GameState"])
        pc = self._get_local_controller(world)
        local_ps = rp(self.pm, pc + self.offsets["AController::PlayerState"]) if pc else 0

        if gamestate:
            pa_data, pa_count = read_array(self.pm, gamestate + self.offsets["AGameStateBase::PlayerArray"])
            for i in range(pa_count):
                ps = rp(self.pm, pa_data + i * 8)
                if not ps or (ps == local_ps and not include_local): continue
                pawn = rp(self.pm, ps + self.offsets["APlayerState::PawnPrivate"])
                if not pawn: continue
                root = rp(self.pm, pawn + self.offsets["AActor::RootComponent"])
                pos = rvec3(self.pm, root + self.offsets["USceneComponent::RelativeLocation"]) if root else (0,0,0)
                if abs(pos[0]) > 0.01:
                    yield (ps == local_ps), pos, i

def w2s(world_pos, camera, sw, sh):
    pitch, yaw, roll = [math.radians(x) for x in camera["rot"]]
    sp, cp, sy, cy, sr, cr = math.sin(pitch), math.cos(pitch), math.sin(yaw), math.cos(yaw), math.sin(roll), math.cos(roll)
    forward = (cp * cy, cp * sy, sp)
    right = (sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp)
    up = (-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp)
    dx, dy, dz = [world_pos[i] - camera["loc"][i] for i in range(3)]
    vx = dx*forward[0] + dy*forward[1] + dz*forward[2]
    vy = dx*right[0] + dy*right[1] + dz*right[2]
    vz = dx*up[0] + dy*up[1] + dz*up[2]
    if vx <= 0.1: return None
    tan_fov = math.tan(math.radians(camera["fov"]) / 2.0)
    screen_x = (1.0 + vy / (vx * tan_fov)) * sw / 2.0
    screen_y = (1.0 - vz / (vx * tan_fov / (sw / sh))) * sh / 2.0
    return (screen_x, screen_y) if (0 <= screen_x <= sw and 0 <= screen_y <= sh) else None

@dataclass
class Config:
    enabled: bool = True
    dot_esp: bool = True
    show_local: bool = False
    show_names: bool = True
    show_distance: bool = True
    snap_lines: bool = True
    enemy_color: Tuple[int, int, int] = (0, 255, 200) 
    local_color: Tuple[int, int, int] = (255, 200, 0)
    dot_radius: int = 6

class Menu(QWidget):
    def __init__(self, config: Config):
        super().__init__()
        self.config = config
        self.setWindowTitle("PRO SOCCER PURE ESP")
        self.setWindowFlags(Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint | Qt.Tool)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setFixedSize(240, 320)
        self._build_ui()

    def _build_ui(self):
        container = QFrame(self)
        container.setStyleSheet("QFrame { background-color: rgba(15, 20, 25, 240); border: 1px solid #00ffcc; border-radius: 6px; } QLabel, QCheckBox { color: #eee; font-family: Consolas; font-size: 11px; } QPushButton { background-color: #222; color: #00ffcc; border: 1px solid #00ffcc; padding: 4px; font-weight: bold; }")
        layout = QVBoxLayout(container)
        
        title = QLabel("PRO SOCCER ESP PANEL")
        title.setStyleSheet("font-size: 12px; font-weight: bold; color: #00ffcc; letter-spacing: 1px;")
        layout.addWidget(title)

        layout.addWidget(self._chk("ESP Katmanı Aktif", "enabled"))
        layout.addWidget(self._chk("Oyuncu Noktaları", "dot_esp"))
        layout.addWidget(self._chk("Kendi Karakterim", "show_local"))
        layout.addWidget(self._chk("Oyuncu İsimleri", "show_names"))
        layout.addWidget(self._chk("Uzaklık Bilgisi (Metre)", "show_distance"))
        layout.addWidget(self._chk("Ekrana Bağlantı Çizgisi", "snap_lines"))

        btn_color = QPushButton("Renk Paletini Aç")
        btn_color.clicked.connect(self._pick_color)
        layout.addWidget(btn_color)
        
        layout.addWidget(QLabel("Insert / F1 : Menüyü Gizle"))
        outer = QVBoxLayout(self)
        outer.addWidget(container)
        self.setLayout(outer)

    def _chk(self, text, attr):
        cb = QCheckBox(text)
        cb.setChecked(getattr(self.config, attr))
        cb.stateChanged.connect(lambda s: setattr(self.config, attr, bool(s)))
        return cb

    def _pick_color(self):
        c = QColorDialog.getColor(QColor(*self.config.enemy_color), self)
        if c.isValid(): self.config.enemy_color = (c.red(), c.green(), c.blue())

class Overlay(QWidget):
    def __init__(self, esp: ProSoccerESP, config: Config):
        super().__init__()
        self.esp, self.config = esp, config
        self.setWindowFlags(Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.WindowTransparentForInput)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setGeometry(0, 0, 1920, 1080)
        
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.update)
        self.timer.start(16) 

    def paintEvent(self, event):
        if not self.config.enabled: return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.setFont(QFont("Consolas", 9, QFont.Bold))
        w, h = self.width(), self.height()
        cam = self.esp.get_camera()
        if not cam: return

        for is_local, pos, idx in self.esp.iter_players(include_local=self.config.show_local):
            s = w2s(pos, cam, w, h)
            if not s: continue
            cx, cy = s
            color = self.config.local_color if is_local else self.config.enemy_color

            if self.config.dot_esp:
                painter.setPen(Qt.NoPen)
                painter.setBrush(QColor(*color))
                painter.drawEllipse(int(cx - self.config.dot_radius), int(cy - self.config.dot_radius), self.config.dot_radius*2, self.config.dot_radius*2)

            if self.config.snap_lines:
                painter.setPen(QPen(QColor(*color), 1, Qt.SolidLine))
                painter.drawLine(int(w / 2), int(h), int(cx), int(cy))

            labels = []
            if self.config.show_names: labels.append("BEN" if is_local else f"Rakip {idx}")
            if self.config.show_distance: labels.append(f"{int(dist(pos, cam['loc'])/100)}m")
            if labels:
                painter.setPen(QPen(QColor(*color)))
                painter.drawText(int(cx + self.config.dot_radius + 4), int(cy + 3), " | ".join(labels))

def main():
    app = QApplication(sys.argv)
    config = Config()
    try: esp = ProSoccerESP()
    except Exception as e:
        print(f"Hata: {e}")
        return
    menu = Menu(config)
    overlay = Overlay(esp, config)
    overlay.show()
    menu.show()

    VK_INSERT, VK_F1 = 0x2D, 0x70
    states = {"ins": False, "f1": False}
    def poll():
        for vk, k in [(VK_INSERT, "ins"), (VK_F1, "f1")]:
            s = bool(ctypes.windll.user32.GetAsyncKeyState(vk) & 0x8000)
            if s and not states[k]: menu.setVisible(not menu.isVisible())
            states[k] = s
    t = QTimer()
    t.timeout.connect(poll)
    t.start(50)
    sys.exit(app.exec_())

if __name__ == "__main__":
    main()
            
