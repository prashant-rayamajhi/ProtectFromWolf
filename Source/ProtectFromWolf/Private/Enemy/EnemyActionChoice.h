#pragma once

#include "CoreMinimal.h"

//実行可能な攻撃の点数を比較し、同点でも候補の追加順に左右されない選択を行うクラス
struct FEnemyActionChoice
{
	//選択された攻撃の識別名
	FName m_action = NAME_None;
	//選択された攻撃の評価点
	float m_score = -BIG_NUMBER;

	//実行できる候補だけを比較し、最高点の攻撃を更新する関数
	void Consider(FName _action, float _score, bool _eligible)
	{
		if (!_eligible || !FMath::IsFinite(_score)) { return; }
		if (_score > m_score || (_score == m_score && (m_action.IsNone() || _action.LexicalLess(m_action))))
		{
			m_score = _score;
			m_action = _action;
		}
	}
};
