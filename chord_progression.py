import math
import re
from functools import lru_cache


# ============================================================
# 七种中古调式的十二平均律映射
# 默认认为，主音=0，主音的高一八度的下一个半音为11
# 其实采用1~12（而非0~11）更方便人类理解？但是要方便计算机理解的话，还是从0开始吧（
# ============================================================

MODE_INTERVALS = {
    "Ionian":     [0, 2, 4, 5, 7, 9, 11],
    "Dorian":     [0, 2, 3, 5, 7, 9, 10],
    "Phrygian":   [0, 1, 3, 5, 7, 8, 10],
    "Lydian":     [0, 2, 4, 6, 7, 9, 11],
    "Mixolydian": [0, 2, 4, 5, 7, 9, 10],
    "Aeolian":    [0, 2, 3, 5, 7, 8, 10],
    "Locrian":    [0, 1, 3, 5, 6, 8, 10],
}

# 定义C大调的所有内音（即所有白键）的十二平局律映射
NATURAL_PC = {
    "C": 0,
    "D": 2,
    "E": 4,
    "F": 5,
    "G": 7,
    "A": 9,
    "B": 11,
}

# 所有和弦模板的十二平均律映射（根音记为0）
# 涵盖：大小三和弦/增减三和弦/挂二四和弦/大小七和弦/属七和弦/半减七和弦/减七和弦
CHORD_TEMPLATES = {
    "major": {0, 4, 7},
    "minor": {0, 3, 7},
    "diminished": {0, 3, 6},
    "augmented": {0, 4, 8},

    "sus2": {0, 2, 7},
    "sus4": {0, 5, 7},

    "dominant7": {0, 4, 7, 10},
    "major7": {0, 4, 7, 11},
    "minor7": {0, 3, 7, 10},
    "half_diminished7": {0, 3, 6, 10},
    "diminished7": {0, 3, 6, 9},
}


# 将str型升降记号映射到int型pitch-class的±1，取模12
def parse_note(note: str) -> int:
    if not re.fullmatch(r"[A-G](?:#|b)?", note):
        raise ValueError(
            f"你好坏！这是非法音名: {note!r}。"
            f"必须形如 C C# Db 喔，且音名字母必须大写捏~"
        )
    pc = NATURAL_PC[note[0]]
    if len(note) == 2:
        if note[1] == "#":
            pc += 1
        elif note[1] == "b":
            pc -= 1
    return pc % 12


# 将调式输入拆分为主音和调式名称
def parse_mode(mode_string: str):
    parts = mode_string.split()
    if len(parts) != 2:
        raise ValueError(
            "调式必须写成类似于 C Ionian 的格式捏~（中间用空格分开）"
        )
    tonic_name, mode_name = parts
    tonic = parse_note(tonic_name)
    if mode_name not in MODE_INTERVALS:
        raise ValueError(
            "调式名称必须严格为以下七种之一捏：\n"
            "Ionian, Dorian, Phrygian, Lydian, "
            "Mixolydian, Aeolian, Locrian"
        )
    return tonic, mode_name, MODE_INTERVALS[mode_name]


# 和弦内音拆分为pitch-class列表（音级关系去重+检测）
def parse_chord(chord_string: str):
    notes = chord_string.split()
    if len(notes) < 2:
        raise ValueError("只有一个音不能算和弦喵！哈！！！")
    pcs = [parse_note(note) for note in notes]
    # 忽略八度重复带来的影响
    # 例如：C E G C 等效为 C E G
    pcs = list(dict.fromkeys(pcs))
    if len(pcs) < 2:
        raise ValueError("只有一个音不能算和弦喵！哈！！！")
    return pcs


# 3. 音级距离（作差取绝对值）
def pitch_class_distance(a: int, b: int) -> int:
    """
    十二平均律圈（而非五度圈）的最短距离。
    例如：
        C -> C# = 1
        C -> B  = 1
        C -> G  = 5
    """
    d = abs(a - b) % 12
    return min(d, 12 - d)


# 动态规划计算两和弦间最小音级移动量之和
def minimum_voice_leading(chord1, chord2):
    """
    在所有可能的声部对应关系中寻找总移动量最小的方案
    为减少复杂度，使用动态规划而不是暴力枚举/全排列
    """
    source = tuple(chord1)
    target = tuple(chord2)
    swapped = False
    # 为减少DP状态，让source永远是较小的集合
    if len(source) > len(target):
        source, target = target, source
        swapped = True
    @lru_cache(maxsize=None)
    def dp(i, used_mask):
        if i == len(source):
            return 0, ()
        best_cost = float("inf")
        best_pairs = ()
        for j, target_note in enumerate(target):
            if used_mask & (1 << j):
                continue
            d = pitch_class_distance(
                source[i],
                target_note
            )
            remaining_cost, remaining_pairs = dp(
                i + 1,
                used_mask | (1 << j)
            )
            total_cost = d + remaining_cost
            if swapped:
                pair = (target_note, source[i])
            else:
                pair = (source[i], target_note)
            if total_cost < best_cost:
                best_cost = total_cost
                best_pairs = (pair,) + remaining_pairs
        return best_cost, best_pairs
    return dp(0, 0)


# 推测和弦根音
def infer_chord_root(chord):
    """
    尝试上面定义的和弦模板（CHORD_TEMPLATES）推测根音
    例如：
        C E G     -> C
        E G C     -> C
        G B D     -> G
        B D F G   -> G（G7）
    无法可靠识别时，退回到输入的第一个音
    """
    chord_set = set(chord)
    best = None
    for root in chord_set:
        relative = {
            (pc - root) % 12
            for pc in chord_set
        }
        for template in CHORD_TEMPLATES.values():
            missing = len(template-relative)
            extra = len(relative-template)

            # 缺少模板核心音的对误差的惩罚更重
            error = missing*2+extra

            candidate = (error, root)
            if best is None or candidate < best:
                best = candidate
    # 无法可靠识别的判据为，误差（error）太大
    if best is None or best[0] > 2:
        return chord[0]
    return best[1]


# 调式一致性检测（调式内音级稳定度）
def tonal_stability(pc, tonic, mode_intervals):
    """
    简化版的 Lerdahl-style tonal hierarchy
    借用 tonal hierarchy / tonal attraction 的思想
    例如：1级最稳定，5级其次，3级再次…
    其他调式内音级较弱，调外音最低
    """
    relative_pc = (pc - tonic) % 12
    if relative_pc not in mode_intervals:
        return 0.10
    degree = mode_intervals.index(relative_pc)
    # 为1级最稳定，7级最不稳定
    stability_by_degree = [
        1.00,   # I
        0.55,   # II
        0.75,   # III
        0.62,   # IV
        0.88,   # V
        0.50,   # VI
        0.42,   # VII
    ]
    return stability_by_degree[degree]


# 7. 根音运动评分
def root_motion_score(root1, root2):
    """
    对两个根音之间的 interval class 评分。

    同音             1.00
    四/五度          1.00
    大/小三度        ~0.83
    大/小二度        ~0.70
    三全音           0.35
    """

    d = pitch_class_distance(root1, root2)

    scores = {
        0: 1.00,
        1: 0.68,
        2: 0.72,
        3: 0.82,
        4: 0.84,
        5: 1.00,
        6: 0.35,
    }

    return scores[d]


# ============================================================
# 8. Tonal Attraction / Resolution
# ============================================================

def attraction_score(
    voice_pairs,
    tonic,
    mode_intervals
):
    """
    衡量声部是否：
    1. 保留共同音
    2. 半音/全音移动
    3. 向更稳定的调式音级移动

    这是一个受 Lerdahl tonal attraction 启发的
    工程化指标，而不是论文中的原始公式。
    """

    scores = []

    for source, target in voice_pairs:

        distance = pitch_class_distance(
            source,
            target
        )

        source_stability = tonal_stability(
            source,
            tonic,
            mode_intervals
        )

        target_stability = tonal_stability(
            target,
            tonic,
            mode_intervals
        )

        stability_gain = max(
            0.0,
            target_stability - source_stability
        )

        # 共同音
        if distance == 0:
            score = 0.65

        # 半音进行：非常强的 voice-leading 连贯性
        elif distance == 1:
            score = (
                0.75
                + 0.25 * stability_gain
            )

        # 全音进行
        elif distance == 2:
            score = (
                0.55
                + 0.25 * stability_gain
            )

        # 三度附近
        elif distance <= 4:
            score = (
                0.30
                + 0.20 * stability_gain
            )

        # 四度以上
        else:
            score = 0.12

        scores.append(
            min(1.0, score)
        )

    return sum(scores) / len(scores)


# ============================================================
# 9. 总评分
# ============================================================

def chord_progression_score(
    mode_string: str,
    chord1_string: str,
    chord2_string: str
) -> float:

    tonic, mode_name, mode_intervals = parse_mode(
        mode_string
    )

    chord1 = parse_chord(chord1_string)
    chord2 = parse_chord(chord2_string)

    # --------------------------------------------------------
    # A. Voice-leading efficiency
    # --------------------------------------------------------

    vl_distance, voice_pairs = minimum_voice_leading(
        chord1,
        chord2
    )

    # 和弦音数不一致时增加轻度 penalty。
    # 例如三和弦 -> 七和弦不应该被视为完全无代价。
    cardinality_difference = abs(
        len(chord1) - len(chord2)
    )

    unmatched_penalty = (
        2.5 * cardinality_difference
    )

    average_voice_distance = (
        vl_distance + unmatched_penalty
    ) / max(
        len(chord1),
        len(chord2)
    )

    # 指数衰减：
    # 平均移动越小，得分越高。
    voice_leading_score = math.exp(
        -average_voice_distance / 2.8
    )

    # --------------------------------------------------------
    # B. Common-tone retention
    # --------------------------------------------------------

    common_tones = len(
        set(chord1) & set(chord2)
    )

    common_tone_score = (
        common_tones
        / min(len(chord1), len(chord2))
    )

    # --------------------------------------------------------
    # C. Modal compatibility
    # --------------------------------------------------------

    mode_pitch_classes = {
        (tonic + interval) % 12
        for interval in mode_intervals
    }

    chord1_mode_ratio = (
        sum(
            pc in mode_pitch_classes
            for pc in chord1
        )
        / len(chord1)
    )

    chord2_mode_ratio = (
        sum(
            pc in mode_pitch_classes
            for pc in chord2
        )
        / len(chord2)
    )

    modal_score = (
        chord1_mode_ratio
        + chord2_mode_ratio
    ) / 2

    # --------------------------------------------------------
    # D. Harmonic root motion
    # --------------------------------------------------------

    root1 = infer_chord_root(chord1)
    root2 = infer_chord_root(chord2)

    harmonic_root_score = root_motion_score(
        root1,
        root2
    )

    # --------------------------------------------------------
    # E. Tonal attraction / resolution
    # --------------------------------------------------------

    resolution_score = attraction_score(
        voice_pairs,
        tonic,
        mode_intervals
    )

    # --------------------------------------------------------
    # 最终加权
    #
    # 45% Voice leading
    # 15% Common tones
    # 15% Modal compatibility
    # 10% Root motion
    # 15% Tonal attraction
    # --------------------------------------------------------

    final_score = 100.0 * (
        0.45 * voice_leading_score
        + 0.15 * common_tone_score
        + 0.15 * modal_score
        + 0.10 * harmonic_root_score
        + 0.15 * resolution_score
    )

    return max(
        0.0,
        min(100.0, final_score)
    )


if __name__ == "__main__":

    mode = input().strip()
    chord1 = input().strip()
    chord2 = input().strip()

    try:
        score = chord_progression_score(
            mode,
            chord1,
            chord2
        )

        print(f"{score:.2f}")

    except ValueError as e:
        print(f"输入错误: {e}")