"""Real-ESRGAN's x4 anime model (RRDBNet, 6 blocks; BSD-3-Clause, github.com/xinntao/Real-ESRGAN) on the CPU."""
import torch
import torch.nn as nn
import torch.nn.functional as F


class RDB(nn.Module):
    def __init__(self, f=64, g=32):
        super().__init__()
        self.conv1 = nn.Conv2d(f, g, 3, 1, 1)
        self.conv2 = nn.Conv2d(f + g, g, 3, 1, 1)
        self.conv3 = nn.Conv2d(f + 2 * g, g, 3, 1, 1)
        self.conv4 = nn.Conv2d(f + 3 * g, g, 3, 1, 1)
        self.conv5 = nn.Conv2d(f + 4 * g, f, 3, 1, 1)
        self.lrelu = nn.LeakyReLU(0.2, True)

    def forward(self, x):
        x1 = self.lrelu(self.conv1(x))
        x2 = self.lrelu(self.conv2(torch.cat((x, x1), 1)))
        x3 = self.lrelu(self.conv3(torch.cat((x, x1, x2), 1)))
        x4 = self.lrelu(self.conv4(torch.cat((x, x1, x2, x3), 1)))
        x5 = self.conv5(torch.cat((x, x1, x2, x3, x4), 1))
        return x5 * 0.2 + x


class RRDB(nn.Module):
    def __init__(self, f=64, g=32):
        super().__init__()
        self.rdb1, self.rdb2, self.rdb3 = RDB(f, g), RDB(f, g), RDB(f, g)

    def forward(self, x):
        return self.rdb3(self.rdb2(self.rdb1(x))) * 0.2 + x


class RRDBNet(nn.Module):
    def __init__(self, nb=6, f=64, g=32):
        super().__init__()
        self.conv_first = nn.Conv2d(3, f, 3, 1, 1)
        self.body = nn.Sequential(*[RRDB(f, g) for _ in range(nb)])
        self.conv_body = nn.Conv2d(f, f, 3, 1, 1)
        self.conv_up1 = nn.Conv2d(f, f, 3, 1, 1)
        self.conv_up2 = nn.Conv2d(f, f, 3, 1, 1)
        self.conv_hr = nn.Conv2d(f, f, 3, 1, 1)
        self.conv_last = nn.Conv2d(f, 3, 3, 1, 1)
        self.lrelu = nn.LeakyReLU(0.2, True)

    def forward(self, x):
        feat = self.conv_first(x)
        feat = feat + self.conv_body(self.body(feat))
        feat = self.lrelu(self.conv_up1(F.interpolate(feat, scale_factor=2, mode='nearest')))
        feat = self.lrelu(self.conv_up2(F.interpolate(feat, scale_factor=2, mode='nearest')))
        return self.conv_last(self.lrelu(self.conv_hr(feat)))


def load(path):
    net = RRDBNet()
    sd = torch.load(path, map_location='cpu', weights_only=True)
    sd = sd.get('params_ema', sd.get('params', sd))
    net.load_state_dict(sd, strict=True)
    return net.eval()


@torch.no_grad()
def upscale(net, rgb):
    """rgb float HxWx3 in 0..1 -> 4x, tiled."""
    H, W, _ = rgb.shape
    x = torch.from_numpy(rgb.transpose(2, 0, 1)).float()[None]
    out = torch.zeros(1, 3, H * 4, W * 4)
    tile, pad = 192, 12
    for y0 in range(0, H, tile):
        for x0 in range(0, W, tile):
            ya, yb = max(0, y0 - pad), min(H, y0 + tile + pad)
            xa, xb = max(0, x0 - pad), min(W, x0 + tile + pad)
            o = net(x[:, :, ya:yb, xa:xb])
            oy, ox = (y0 - ya) * 4, (x0 - xa) * 4
            h, w = (min(H, y0 + tile) - y0) * 4, (min(W, x0 + tile) - x0) * 4
            out[:, :, y0 * 4:y0 * 4 + h, x0 * 4:x0 * 4 + w] = o[:, :, oy:oy + h, ox:ox + w]
    return out[0].clamp(0, 1).numpy().transpose(1, 2, 0)
